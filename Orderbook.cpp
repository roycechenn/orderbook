#include "Orderbook.h"

#include <chrono>
#include <ctime>
#include <numeric>

void OrderBook::PruneGoodForDayOrders() {

    using namespace std::chrono;
    const auto end = hours(24); // midnight

    while (true) {
        const auto now = system_clock::now();
        const auto now_c = system_clock::to_time_t(now);
        std::tm now_parts;
        localtime_r(&now_c, &now_parts);

        if (now_parts.tm_hour >= end.count()) now_parts.tm_mday += 1;

        now_parts.tm_hour = end.count();
        now_parts.tm_min = 0;
        now_parts.tm_sec = 0;

        auto next = system_clock::from_time_t(mktime(&now_parts));
        auto till = next - now + milliseconds(100);

        {
            std::unique_lock ordersLock{ordersMutex_};

            if (shutdown_.load(std::memory_order_acquire) ||
                shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
                return;
        }

        OrderIds orderIds;

        {
            std::scoped_lock orderLock{ordersMutex_};
            for (const auto &[_, entry] : orders_) {
                const auto &[order, unused_iterator] = entry;

                if (order->GetOrderType() != OrderType::GoodForDay) continue;

                orderIds.push_back(order->GetOrderId());
            }
        }

        CancelOrders(orderIds);
    }
}

void OrderBook::CancelOrders(OrderIds orderIds) {

    std::scoped_lock orderLock(ordersMutex_);
    for (const auto &orderId : orderIds)
        CancelOrderInternal(orderId); // do this because of locking, more effective taking mutex once
}

void OrderBook::CancelOrderInternal(OrderId orderId) {
    if (!orders_.contains(orderId)) return;

    const auto entry = orders_.at(orderId);
    orders_.erase(orderId);
    const auto &order = entry.order_;
    const auto iterator = entry.location_;

    if (order->GetSide() == Side::Sell) {
        if (auto it = asks_.find(order->GetPrice()); it != asks_.end()) {
            auto &orders = it->second;
            orders.erase(iterator);
            if (orders.empty()) asks_.erase(it);
        }
    } else {
        if (auto it = bids_.find(order->GetPrice()); it != bids_.end()) {
            auto &orders = it->second;
            orders.erase(iterator);
            if (orders.empty()) bids_.erase(it);
        }
    }

    OnOrderCancelled(order);
}

void OrderBook::OnOrderCancelled(OrderPointer order) {
    UpdateLevelUpdate(order->GetPrice(), order->GetRemainingQty(), LevelData::Action::Remove);
}

void OrderBook::OnOrderAdded(OrderPointer order) {
    UpdateLevelUpdate(order->GetPrice(), order->GetInitialQty(), LevelData::Action::Add);
}

void OrderBook::OnOrderMatched(Price price, Qty qty, bool isFullyFilled) {
    UpdateLevelUpdate(price, qty, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void OrderBook::UpdateLevelUpdate(Price price, Qty qty, LevelData::Action action) {
    auto &data = data_[price];

    data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;

    if (action == LevelData::Action::Remove || action == LevelData::Action::Match) {
        data.qty_ -= qty;
    } else {
        data.qty_ += qty;
    }

    if (data.count_ == 0) data_.erase(price);
}

bool OrderBook::CanFullyFill(Side side, Price price, Qty qty) const {
    if (!CanMatch(side, price)) return false;

    std::optional<Price> threshold; // best price for fill and kill, not entire book

    if (side == Side::Buy) {
        const auto [askPrice, _] = *asks_.begin(); // cant be empty
        threshold = askPrice;
    } else {
        const auto [bidPrice, _] = *bids_.begin();
        threshold = bidPrice;
    }

    for (const auto &[levelPrice, LevelData] : data_) {
        if (threshold.has_value() &&
            (side == Side::Buy && threshold.value() > levelPrice || (side == Side::Sell && threshold.value() < levelPrice)))
            continue;
        if ((side == Side::Buy && levelPrice > price) || (side == Side::Sell && levelPrice < price)) continue;
        if (qty <= LevelData.qty_) return true;
        qty -= LevelData.qty_;
    }

    return false;
}

bool OrderBook::CanMatch(Side side, Price price) const {
    if (side == Side::Buy) {
        if (asks_.empty()) return false;
        const auto &[bestAsk, _] = *asks_.begin(); // best price
        return price >= bestAsk;
    } else {
        if (bids_.empty()) return false;
        const auto &[bestBid, _] = *bids_.begin();
        return price <= bestBid;
    }
}

Trades OrderBook::MatchOrders() { // same bid/ask price
    Trades trades;
    trades.reserve(orders_.size()); // if every bid/ask is the same price

    while (true) {
        if (bids_.empty() || asks_.empty()) {
            break;
        }
        auto &[bidPrice, bids] = *bids_.begin();
        auto &[askPrice, asks] = *asks_.begin();

        if (bidPrice < askPrice) {
            break;
        }

        while (bids.size() && asks.size()) {
            auto &bid = bids.front();
            auto &ask = asks.front();

            Qty qty = std::min(bid->GetRemainingQty(), ask->GetRemainingQty());
            bid->Fill(qty);
            ask->Fill(qty);

            if (bid->IsFilled()) {
                bids.pop_front();
                orders_.erase(bid->GetOrderId());
            }

            if (ask->IsFilled()) {
                asks.pop_front();
                orders_.erase(ask->GetOrderId());
            }

            trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), qty},
                                   TradeInfo{ask->GetOrderId(), ask->GetPrice(), qty}});

            OnOrderMatched(bid->GetPrice(), qty, bid->IsFilled());
            OnOrderMatched(ask->GetPrice(), qty, ask->IsFilled());
        };

        if (bids.empty()) {
            bids_.erase(bidPrice);
            data_.erase(bidPrice);
        }
        if (asks.empty()) {
            asks_.erase(askPrice);
            data_.erase(askPrice);
        }
    }

    return trades;
};

OrderBook::OrderBook() : ordersPruneThread_{[this] { PruneGoodForDayOrders(); }} {}

OrderBook::~OrderBook() {
    shutdown_.store(true, std::memory_order_release);
    shutdownConditionVariable_.notify_one();
    ordersPruneThread_.join();
}

Trades OrderBook::AddOrder(OrderPointer order) {
    std::scoped_lock orderLock{ordersMutex_};

    if (orders_.contains(order->GetOrderId())) return {};

    if (order->GetOrderType() == OrderType::Market) {
        if (order->GetSide() == Side::Buy && !asks_.empty()) {
            const auto &[worstAsk, _] = *asks_.rbegin();
            order->ToGoodTillCancel(worstAsk);
        } else if (order->GetSide() == Side::Sell && !bids_.empty()) {
            const auto &[worstBid, _] = *bids_.rbegin();
            order->ToGoodTillCancel(worstBid);
        } else
            return {};
    }

    if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice())) return {};
    if (order->GetOrderType() == OrderType::FillAndKill &&
        !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQty()))
        return {};

    OrderPointers::iterator iterator;

    if (order->GetSide() == Side::Buy) {
        auto &orders = bids_[order->GetPrice()];
        iterator = orders.insert(orders.end(), order);
    } else {
        auto &orders = asks_[order->GetPrice()];
        iterator = orders.insert(orders.end(), order);
    }

    orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});
    OnOrderAdded(order);
    return MatchOrders();
}

void OrderBook::CancelOrder(OrderId orderId) {
    std::scoped_lock ordersLock{ordersMutex_};

    CancelOrderInternal(orderId);
};

Trades OrderBook::MatchOrder(OrderModify order) {
    if (!orders_.contains(order.GetOrderId())) return {};

    const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
    return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
}

std::size_t OrderBook::Size() const { return orders_.size(); }

OrderbookLevelInfos OrderBook::GetOrderInfos() const {
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(orders_.size()); // change to bid size
    askInfos.reserve(orders_.size());

    auto CreateLevelInfos = [](Price price, const OrderPointers &orders) {
        return LevelInfo{price,
                         std::accumulate(orders.begin(), orders.end(), (Qty)0, [](Qty runningSum, const OrderPointer &order) {
                             return runningSum + order->GetRemainingQty();
                         })};
    };
    for (const auto &[price, orders] : bids_)
        bidInfos.push_back(CreateLevelInfos(price, orders));
    for (const auto &[price, orders] : asks_)
        askInfos.push_back(CreateLevelInfos(price, orders));

    return OrderbookLevelInfos{bidInfos, askInfos};
}
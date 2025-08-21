#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <stack>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

enum class OrderType { GoodTillCancel, FillAndKill };

enum class Side { Buy, Sell };

using Price = std::int32_t; // "Price" is now a type
using Qty = std::uint32_t;
using OrderId = std::uint64_t;

struct LevelInfo {
    Price price_;
    Qty qty_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos {
  private:
    LevelInfos bids_;
    LevelInfos asks_;

  public:
    OrderbookLevelInfos(const LevelInfos &bids, const LevelInfos &asks) : bids_{bids}, asks_{asks} {}

    const LevelInfos &GetBids() const { return bids_; }
    const LevelInfos &GetAsks() const { return asks_; }
};

class Order {
  private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Qty initialQty_;
    Qty remainingQty_;

  public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Qty qty)
        : orderType_{orderType}, orderId_{orderId}, side_{side}, price_{price}, initialQty_{qty}, remainingQty_{qty} {
        if (qty == 0) throw std::invalid_argument("Quantity must be positive");
    }

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType() const { return orderType_; }
    Qty GetInitialQty() const { return initialQty_; }
    Qty GetRemainingQty() const { return remainingQty_; }
    Qty GetFilledQty() const { return GetInitialQty() - GetRemainingQty(); }
    bool IsFilled() const { return GetRemainingQty() == 0; }
    void Fill(Qty qty) {
        if (qty > GetRemainingQty())
            throw std::logic_error(std::format("Order({}) cannot be filled for more than its remaining qty.", GetOrderId()));
        remainingQty_ -= qty;
    }
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify {
  private:
    OrderId orderId_;
    Price price_;
    Side side_;
    Qty qty_;

  public:
    OrderModify(OrderId orderId, Side side, Price price, Qty qty)
        : orderId_{orderId}, price_{price}, side_{side}, qty_{qty} {}

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    Qty GetQty() const { return qty_; }

    OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQty());
    }
};

struct TradeInfo {
    OrderId orderId_;
    Price price_;
    Qty qty_;
};

class Trade {
  private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;

  public:
    Trade(const TradeInfo &bidTrade, const TradeInfo &askTrade) : bidTrade_{bidTrade}, askTrade_{askTrade} {}
    const TradeInfo &GetBidTrade() const { return bidTrade_; }
    const TradeInfo &GetAskTrade() const { return askTrade_; }
};

using Trades = std::vector<Trade>;

class OrderBook {
  private:
    struct OrderEntry {
        OrderPointer order_{nullptr};
        OrderPointers::iterator location_;
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    bool CanMatch(Side side, Price price) const {
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

    Trades MatchOrders() { // same bid/ask price
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
                    asks.pop_back();
                    orders_.erase(ask->GetOrderId());
                }

                if (bids.empty()) bids_.erase(bidPrice);

                if (asks.empty()) asks_.erase(askPrice);

                trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), qty},
                                       TradeInfo{ask->GetOrderId(), ask->GetPrice(), qty}

                });
            };
        }

        if (!bids_.empty()) {
            auto &[_, bids] = *bids_.begin();
            auto &order = bids.front();
            if (order->GetOrderType() == OrderType::FillAndKill) CancelOrder(order->GetOrderId());
        }

        if (!bids_.empty()) {
            auto &[_, asks] = *asks_.begin();
            auto &order = asks.front();
            if (order->GetOrderType() == OrderType::FillAndKill) CancelOrder(order->GetOrderId());
        }

        return trades;
    };

  public:
    Trades AddOrder(OrderPointer order) {
        if (orders_.contains(order->GetOrderId())) return {};
        if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice())) return {};

        OrderPointers::iterator iterator;
        if (order->GetSide() == Side::Buy) {
            auto &orders = bids_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        } else {
            auto &orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }

        orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});
        return MatchOrders();
    }

    void CancelOrder(OrderId orderId) {
        if (!orders_.contains(orderId)) return;

        const auto &[order, iterator] = orders_.at(orderId);
        orders_.erase(orderId);

        if (order->GetSide() == Side::Sell) {
            auto price = order->GetPrice();
            auto &orders = asks_.at(price);
            orders.erase(iterator);
            if (orders.empty()) asks_.erase(price);
        } else {
            auto price = order->GetPrice();
            auto &orders = bids_.at(price);
            orders.erase(iterator);
            if (orders.empty()) bids_.erase(price);
        }
    };

    Trades MatchOrder(OrderModify order) {
        if (!orders_.contains(order.GetOrderId())) return {};

        const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
        return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
    }

    std::size_t Size() const { return orders_.size(); }

    OrderbookLevelInfos GetOrderInfos() const {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders_.size()); // change to bid size
        askInfos.reserve(orders_.size());

        auto CreateLevelInfos = [](Price price, const OrderPointers &orders) {
            return LevelInfo{
                price, std::accumulate(orders.begin(), orders.end(), (Qty)0, [](Qty runningSum, const OrderPointer &order) {
                    return runningSum + order->GetRemainingQty();
                })};
        };
        for (const auto &[price, orders] : bids_)
            bidInfos.push_back(CreateLevelInfos(price, orders));
        for (const auto &[price, orders] : asks_)
            askInfos.push_back(CreateLevelInfos(price, orders));

        return OrderbookLevelInfos{bidInfos, askInfos};
    }
};

int main() {
    OrderBook orderbook;
    const OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << std::endl;
    orderbook.CancelOrder(orderId);
    std::cout << orderbook.Size() << std::endl;

    return 0;
}
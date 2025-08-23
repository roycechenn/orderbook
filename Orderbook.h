#pragma once

#include "Trade.h"
#include "OrderModify.h"
#include "LevelInfo.h"
#include "OrderBookLevelInfos.h"

#include <numeric>
#include <map>


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
                    asks.pop_front();
                    orders_.erase(ask->GetOrderId());
                }

                if (bids.empty()) bids_.erase(bidPrice);

                if (asks.empty()) asks_.erase(askPrice);

                trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), qty},
                                       TradeInfo{ask->GetOrderId(), ask->GetPrice(), qty}

                });
            };
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
            iterator = orders.insert(orders.end(), order);
        } else {
            auto &orders = asks_[order->GetPrice()];
            iterator = orders.insert(orders.end(), order);
        }

        orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});
        return MatchOrders();
    }

    void CancelOrder(OrderId orderId) {
        if (!orders_.contains(orderId)) return;

        const auto entry = orders_.at(orderId);
        orders_.erase(orderId);
        const auto& order = entry.order_;
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
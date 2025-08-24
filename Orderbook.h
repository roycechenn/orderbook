#pragma once

#include "LevelInfo.h"
#include "OrderBookLevelInfos.h"
#include "OrderModify.h"
#include "Trade.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <numeric>
#include <thread>
#include <unordered_map>

class OrderBook {
  private:
    struct OrderEntry {
        OrderPointer order_{nullptr};
        OrderPointers::iterator location_;
    };
    struct LevelData {
        Qty qty_{};
        Qty count_{};
        enum class Action { Add, Remove, Match };
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;
    mutable std::mutex ordersMutex_;
    std::thread ordersPruneThread_;
    std::atomic<bool> shutdown_{false};

    void PruneGoodForDayOrders();

    void CancelOrders(OrderIds, OrderIds);
    void CancelOrderInternal(OrderId OrderId);

    void OnOrderCancelled(OrderPointer order);
    void OnOrderAdded(OrderPointer order);
    void OnOrderMatched(Price price, Qty qty, bool isFullyFilled);
    void UpdateLevelUpdate(Price price, Qty qty, LevelData::Action action);

    bool CanFullyFill(Side side, Price price, Qty qty) const;
    bool CanMatch(Side side, Price price) const;
    Trades MatchOrders();

  public:
    Trades AddOrder(OrderPointer order) {}
    void CancelOrder(OrderId orderId) {};
    Trades MatchOrder(OrderModify order) {}
    std::size_t Size() const { return orders_.size(); }
    OrderbookLevelInfos GetOrderInfos() const {}
};
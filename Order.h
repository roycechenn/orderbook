#pragma once

#include "Using.h"
#include "OrderType.h"
#include "Side.h"

#include <format>
#include <exception>
#include <list>

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

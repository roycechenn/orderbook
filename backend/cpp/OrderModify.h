#pragma once

#include "Order.h"

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
#pragma once

#include "Constants.h"
#include "OrderType.h"
#include "Side.h"
#include "Using.h"

#include <cmath>
#include <exception>
#include <sstream>
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

    Order(OrderId orderId, Side side, Qty qty) : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, qty) {}

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType() const { return orderType_; }
    Qty GetInitialQty() const { return initialQty_; }
    Qty GetRemainingQty() const { return remainingQty_; }
    Qty GetFilledQty() const { return GetInitialQty() - GetRemainingQty(); }
    bool IsFilled() const { return GetRemainingQty() == 0; }
    void Fill(Qty qty) {
        if (qty > GetRemainingQty()) {
            std::ostringstream oss;
            oss << "Order(" << GetOrderId() << ") cannot be filled for more than its remaining qty.";
            throw std::logic_error(oss.str());
        }
        remainingQty_ -= qty;
    }
    void ToGoodTillCancel(Price price) {
        if (GetOrderType() != OrderType::Market) {
            std::ostringstream oss;
            oss << "Order (" << GetOrderId() << ") cannot have its price adjusted, only market orders can.";
            throw std::logic_error(oss.str());
        }

        price_ = price;
        orderType_ = OrderType::GoodTillCancel;
    }
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

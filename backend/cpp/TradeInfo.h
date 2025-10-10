#pragma once

#include "Order.h"

struct TradeInfo {
    OrderId orderId_;
    Price price_;
    Qty qty_;
};

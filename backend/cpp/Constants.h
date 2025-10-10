#pragma once

#include <limits>

#include "Using.h"

struct Constants {
    static constexpr Price InvalidPrice = std::numeric_limits<Price>::min();
};
#pragma once

#include "Using.h"

struct LevelInfo {
    Price price_;
    Qty qty_;
};

using LevelInfos = std::vector<LevelInfo>;
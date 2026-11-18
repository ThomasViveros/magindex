#pragma once

#include <cstdint>

enum class UpdateType : std::uint8_t {
    None,
    Incremental,
    IncrementalUsingBulk,
    TotalBulk
};

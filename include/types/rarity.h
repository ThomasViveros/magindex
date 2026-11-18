#pragma once
#include <cstdint>

namespace magindex::types {
    enum Rarity : uint8_t {
        Common,
        Uncommon,
        Rare,
        Mythic,
        //Timeshifted?
    };
}
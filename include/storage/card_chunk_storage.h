#pragma once

#include <vector>

#include "storage_interface.h"
#include "types/card.h"


namespace magindex::storage {

    using CardChunk = std::vector<types::Card>;

    class CardChunkStorage : public StorageBase<CardChunk> {
    public:
        ~CardChunkStorage() override = default;
    };
}

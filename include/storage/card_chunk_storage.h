#pragma once

#include "storage_interface.h"
#include "types/card.h"


namespace magindex::storage {

    class CardChunk {
    public:
        std::map<std::string, types::OracleSet> Sets;
        std::map<std::string, types::OracleCard> cards;
    };

    class CardChunkStorage : public StorageBase<CardChunk> {
    public:
        ~CardChunkStorage() override = default;
    };
}

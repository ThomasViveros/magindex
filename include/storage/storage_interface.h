#pragma once
#include <cstdint>
#include <map>
#include <string>
#include "types/query_builder.h"
#include "types/oracle_card.h"
#include "types/update_type.h"

namespace magindex::storage {

    template<typename T>
    class StorageBase {
    public:
        StorageBase() = default;

        virtual ~StorageBase() = default;

        /**
         * @param data The card data to be stored
         * @param updateType The replacement flow used for this data batch.
         */
        virtual void Store(const T &data, ::UpdateType updateType) = 0;

        /**
         * Drops all cards from our storage.
         */
        virtual void EmptyStorage() = 0;

        //Gets all the tracked sets and how much cards we have for that set.
        virtual void GetAllTrackedSets(std::map<std::string, uint64_t>& OUTTrackedSets) = 0;

        virtual types::QueryPage QueryForCards(const types::CardQuery &query,
                                               types::PageRequest page = {}) = 0;
    };
}

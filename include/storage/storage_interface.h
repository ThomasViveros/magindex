#pragma once
#include "types/card.h"
#include "types/query_builder.h"

namespace magindex::storage {
    template<typename T>
    class StorageBase {
    public:
        StorageBase() = default;

        virtual ~StorageBase() = default;

        virtual void Store(const T &data) = 0;

        /**
         * Drops all cards from our storage.
         */
        virtual void EmptyStorage() = 0;

        virtual void QueryForCards(const types::CardQuery &Query, std::vector<types::Card> &OUTCards) = 0;
    };
}

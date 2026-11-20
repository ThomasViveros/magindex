#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <memory>
#include <SQLiteCpp/Database.h>

#include "storage/card_chunk_storage.h"

namespace magindex::storage {


    class SQLiteStorage final : public CardChunkStorage {
        public:

        class Impl {
        public:
            Impl() = delete;
            explicit Impl(const std::filesystem::path &databasePath);

            SQLite::Database database;
        };


        SQLiteStorage() = delete;

        explicit SQLiteStorage(const std::filesystem::path& databasePath);
        explicit SQLiteStorage(std::unique_ptr<Impl> databaseConnection);

        ~SQLiteStorage() override;

        SQLiteStorage(const SQLiteStorage &) = delete;

        SQLiteStorage &operator=(const SQLiteStorage &) = delete;

        void Store(const CardChunk &cards, StoreType storeType) override;

        void EmptyStorage() override;

        void GetAllTrackedSets(std::map<std::string, std::uint64_t> &OUTTrackedSets) override;

        types::QueryPage QueryForCards(const types::CardQuery &query,
                                       types::PageRequest page = {}) override;

    private:

        std::unique_ptr<Impl> impl_;
    };
}

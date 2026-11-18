#pragma once

#include <filesystem>
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

        void Store(const CardChunk &cards) override;

        void EmptyStorage() override;

        void QueryForCards(const types::CardQuery &Query, std::vector<types::Card> &OUTCards) override;

    private:

        std::unique_ptr<Impl> impl_;
    };
}

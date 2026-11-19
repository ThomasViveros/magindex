#include "storage/sqlite/sqlite_storage.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

using namespace magindex::types;

namespace {
    constexpr int SCHEMA_VERSION = 1;

    using QueryValue = std::variant<std::string, int, double>;

    void AddCondition(std::string &sql, std::vector<QueryValue> &values,
                      std::string condition, QueryValue value) {
        sql += " AND (" + std::move(condition) + ")";
        values.push_back(std::move(value));
    }

    std::string ComparisonSql(EqualityOperators op) {
        switch (op) {
            case EqualityOperators::GreaterThan: return ">";
            case EqualityOperators::GreaterThanOrEqualTo: return ">=";
            case EqualityOperators::LessThan: return "<";
            case EqualityOperators::LessThanOrEqualTo: return "<=";
            case EqualityOperators::EqualTo: return "=";
        }
        throw std::invalid_argument("Invalid numeric comparison operator");
    }

    std::string RarityText(Rarity rarity) {
        switch (rarity) {
            case Rarity::Common: return "common";
            case Rarity::Uncommon: return "uncommon";
            case Rarity::Rare: return "rare";
            case Rarity::Mythic: return "mythic";
        }
        throw std::invalid_argument("Invalid rarity value");
    }

    void BindQueryValues(SQLite::Statement &statement, const std::vector<QueryValue> &values) {
        int index = 1;
        for (const auto &value : values) {
            std::visit([&](const auto &item) { statement.bind(index++, item); }, value);
        }
    }

    void AddNumericConditions(std::string &sql, std::vector<QueryValue> &values,
                              const std::vector<NumericalQuery> &queries, std::string_view column,
                              std::string_view faceColumn) {
        if (queries.empty()) return;
        std::string expression;
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto &[op, number, logic] = queries[i];
            if (i != 0) {
                if (logic != LogicalOperators::And && logic != LogicalOperators::Or)
                    throw std::invalid_argument("Invalid numeric logical operator");
                expression += logic == LogicalOperators::And ? " AND " : " OR ";
            }
            expression += "(" + std::string(column) + ComparisonSql(op) +
                          "? OR EXISTS (SELECT 1 FROM card_faces f WHERE f.card_id=c.oracle_id AND f." +
                          std::string(faceColumn) + ComparisonSql(op) + "?))";
            values.emplace_back(number);
            values.emplace_back(number);
        }
        sql += " AND (" + expression + ")";
    }

    void Reset(SQLite::Statement &statement) {
        statement.reset();
        statement.clearBindings();
    }

    void BindNullableText(SQLite::Statement &statement, int index, const std::string &value) {
        if (value.empty())
            statement.bind(index);
        else
            statement.bind(index, value);
    }

    std::optional<double> ParseNumericValue(const std::string &value) {
        if (value.empty())
            return std::nullopt;

        double numericValue = 0.0;
        const char *begin = value.data();
        const char *end = begin + value.size();
        const auto [parsedEnd, error] = std::from_chars(begin, end, numericValue);
        if (error != std::errc{} || parsedEnd != end || !std::isfinite(numericValue))
            return std::nullopt;
        return numericValue;
    }

    void BindNullableNumeric(SQLite::Statement &statement, int index, const std::string &value) {
        if (const std::optional<double> numericValue = ParseNumericValue(value))
            statement.bind(index, *numericValue);
        else
            statement.bind(index);
    }

    std::string FormatDate(std::chrono::year_month_day date) {
        if (!date.ok())
            throw std::invalid_argument("Card release date is invalid or missing");

        std::ostringstream value;
        value << std::setfill('0')
                << std::setw(4) << static_cast<int>(date.year()) << '-'
                << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
                << std::setw(2) << static_cast<unsigned>(date.day());
        return value.str();
    }

    void CreateSchema(SQLite::Database &database) {
        int currentVersion = 0; {
            SQLite::Statement versionQuery(database, "PRAGMA user_version");
            if (!versionQuery.executeStep())
                throw std::runtime_error("Unable to read SQLite schema version");
            currentVersion = versionQuery.getColumn(0).getInt();
        }

        if (currentVersion > SCHEMA_VERSION)
            throw std::runtime_error("Database schema is newer than this Magindex build supports");
        if (currentVersion == SCHEMA_VERSION)
            return;

        SQLite::Transaction transaction(database);

        //Cards table
        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS cards(
                oracle_id TEXT PRIMARY KEY NOT NULL,
                name TEXT NOT NULL,
                layout TEXT NOT NULL DEFAULT '',
                mana_cost TEXT NOT NULL DEFAULT '',
                color_identity INTEGER NOT NULL DEFAULT 0,
                colors INTEGER NOT NULL DEFAULT 0,
                cmc REAL NOT NULL DEFAULT 0,
                type_line TEXT NOT NULL DEFAULT '',
                power TEXT NULL,
                power_numeric REAL,
                toughness TEXT NULL,
                toughness_numeric REAL,
                defense TEXT NULL,
                defense_numeric REAL NULL,
                loyalty TEXT NULL,
                loyalty_numeric REAL,
                oracle_text TEXT NULL
            ) WITHOUT ROWID
        )sql");

        //Sets table
        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS sets(
                set_id TEXT PRIMARY KEY NOT NULL,
                code TEXT NOT NULL,
                name TEXT NOT NULL,
                released_at DATE NOT NULL,
                set_type TEXT NOT NULL
            ) WITHOUT ROWID
        )sql");

        //Printings table
        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS printings(
                scryfall_id TEXT PRIMARY KEY NOT NULL,
                oracle_id TEXT NOT NULL REFERENCES cards(oracle_id) ON DELETE CASCADE,
                set_id TEXT NOT NULL REFERENCES sets(set_id),
                lang TEXT NOT NULL,
                collector_number TEXT NOT NULL,
                flavor_name TEXT NULL,
                artist TEXT NULL,
                frame TEXT NULL,
                border_color TEXT,
                digital BOOLEAN,
                full_art BOOLEAN,
                oversized BOOLEAN,
                promo BOOLEAN,
                textless BOOLEAN,
                reprint BOOLEAN,
                rarity TEXT NOT NULL,
                image_small TEXT NULL,
                image_normal TEXT NULL
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS card_faces (
                card_id TEXT NOT NULL REFERENCES cards(oracle_id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                name TEXT NOT NULL,
                artist TEXT NULL,
                artist_id TEXT NULL,
                colors INTEGER NOT NULL DEFAULT 0,
                cmc FLOAT NULL,
                mana_cost TEXT NULL,
                defense TEXT NULL,
                defense_numeric REAL NULL,
                power TEXT NULL,
                power_numeric REAL NULL,
                toughness TEXT NULL,
                toughness_numeric REAL NULL,
                loyalty TEXT NULL,
                loyalty_numeric REAL NULL,
                type_line TEXT NULL,
                image_small TEXT NULL,
                image_normal TEXT NULL,
                PRIMARY KEY (card_id, ordinal)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS printing_faces (
                printing_id TEXT NOT NULL REFERENCES printings(scryfall_id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                name TEXT NOT NULL,
                artist TEXT NULL,
                artist_id TEXT NULL,
                image_small TEXT NULL,
                image_normal TEXT NULL,
                PRIMARY KEY (printing_id, ordinal)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS finishes (
                printing_id TEXT NOT NULL REFERENCES printings(scryfall_id) ON DELETE CASCADE,
                finish TEXT NOT NULL,
                PRIMARY KEY (printing_id, finish)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS promo_types (
                printing_id TEXT NOT NULL REFERENCES printings(scryfall_id) ON DELETE CASCADE,
                promo_type TEXT NOT NULL,
                PRIMARY KEY (printing_id, promo_type)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS frame_effects (
                printing_id TEXT NOT NULL REFERENCES printings(scryfall_id) ON DELETE CASCADE,
                effect TEXT NOT NULL,
                PRIMARY KEY (printing_id, effect)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS legalities (
                card_id TEXT NOT NULL REFERENCES cards(oracle_id) ON DELETE CASCADE,
                format TEXT NOT NULL,
                legality TEXT NOT NULL,
                PRIMARY KEY (card_id, format)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS keywords (
                card_id TEXT NOT NULL REFERENCES cards(oracle_id) ON DELETE CASCADE,
                keyword TEXT NOT NULL,
                PRIMARY KEY (card_id, keyword)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS artist_ids (
                printing_id TEXT NOT NULL REFERENCES printings(scryfall_id) ON DELETE CASCADE,
                artist_id TEXT NOT NULL,
                PRIMARY KEY (printing_id, artist_id)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS supertypes (
                card_id TEXT NOT NULL REFERENCES cards(oracle_id) ON DELETE CASCADE,
                supertype TEXT NOT NULL,
                PRIMARY KEY (card_id, supertype)
            ) WITHOUT ROWID
        )sql");

        database.exec(R"sql(
            CREATE TABLE IF NOT EXISTS subtypes (
                card_id TEXT NOT NULL REFERENCES cards(oracle_id) ON DELETE CASCADE,
                subtype TEXT NOT NULL,
                PRIMARY KEY (card_id, subtype)
            ) WITHOUT ROWID
        )sql");


        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_name ON cards(name COLLATE NOCASE)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_color ON cards(color_identity)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_colors ON cards(colors)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_cmc ON cards(cmc)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_power ON cards(power_numeric)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_toughness ON cards(toughness_numeric)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_loyalty ON cards(loyalty_numeric)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_cards_defense ON cards(defense_numeric)");

        database.exec("CREATE INDEX IF NOT EXISTS idx_card_faces_name ON card_faces(name COLLATE NOCASE)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_card_faces_color ON card_faces(colors)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_card_faces_power ON card_faces(power_numeric)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_card_faces_toughness ON card_faces(toughness_numeric)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_card_faces_loyalty ON card_faces(loyalty_numeric)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_card_faces_defense ON card_faces(defense_numeric)");

        database.exec("CREATE INDEX IF NOT EXISTS idx_printings_flavor_name ON printings(flavor_name)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_printings_oracle_id ON printings(oracle_id)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_printings_set_id ON printings(set_id)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_artist_ids_value ON artist_ids(artist_id)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_keywords_value ON keywords(keyword)");

        database.exec("CREATE INDEX IF NOT EXISTS idx_sets_name ON sets(name)");
        database.exec("CREATE INDEX IF NOT EXISTS idx_sets_released_at ON sets(released_at)");

        database.exec(std::format("PRAGMA user_version = {}", SCHEMA_VERSION));
        transaction.commit();
    }
}

namespace magindex::storage {

    SQLiteStorage::Impl::Impl(const std::filesystem::path &databasePath) : database(databasePath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
        database.exec("PRAGMA foreign_keys = ON");
        database.exec("PRAGMA journal_mode = WAL");
        database.exec("PRAGMA busy_timeout = 5000");
        CreateSchema(database);
    }

    SQLiteStorage::SQLiteStorage(const std::filesystem::path& databasePath) {
        try {
            if (databasePath.empty())
                throw std::invalid_argument("Database path cannot be empty");
            if (databasePath != std::filesystem::path(":memory:") && !databasePath.parent_path().empty()) {
                std::error_code error;
                std::filesystem::create_directories(databasePath.parent_path(), error);
                if (error)
                    throw std::runtime_error("Unable to create database directory: " + error.message());
            }
            impl_ = std::make_unique<Impl>(databasePath);
        } catch (const std::exception &error) {
            throw std::runtime_error("Failed to initialize SQLite database '" + databasePath.string() + "': " +
                                     error.what());
        }
    }

    SQLiteStorage::SQLiteStorage(std::unique_ptr<Impl> databaseConnection) {
        if (!databaseConnection) throw std::invalid_argument("Database connection cannot be null");

        impl_ = std::move(databaseConnection);
    }


    SQLiteStorage::~SQLiteStorage() = default;

    void SQLiteStorage::Store(const CardChunk &cards, bool bOverrideExisting) {
        if (cards.empty())
            return;

        try {
            SQLite::Database &database = impl_->database;
            SQLite::Transaction transaction(database);

            std::string conflictProtocol = bOverrideExisting ? "DO UPDATE SET" : "DO NOTHING";

            SQLite::Statement upsertCard(database, std::format(R"sql(
            INSERT INTO cards (
                oracle_id, name, layout, mana_cost, color_identity, colors, cmc,
                type_line, power, power_numeric, toughness, toughness_numeric,
                defense, defense_numeric, loyalty, loyalty_numeric, oracle_text
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(oracle_id) {}
                name = excluded.name,
                layout = excluded.layout,
                mana_cost = excluded.mana_cost,
                color_identity = excluded.color_identity,
                colors = excluded.colors,
                cmc = excluded.cmc,
                type_line = excluded.type_line,
                power = excluded.power,
                power_numeric = excluded.power_numeric,
                toughness = excluded.toughness,
                toughness_numeric = excluded.toughness_numeric,
                defense = excluded.defense,
                defense_numeric = excluded.defense_numeric,
                loyalty = excluded.loyalty,
                loyalty_numeric = excluded.loyalty_numeric,
                oracle_text = excluded.oracle_text
        )sql", conflictProtocol));
            SQLite::Statement upsertSet(database, std::format(R"sql(
            INSERT INTO sets(set_id, code, name, released_at, set_type)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(set_id) {}
                code = excluded.code,
                name = excluded.name,
                released_at = excluded.released_at,
                set_type = excluded.set_type
        )sql", conflictProtocol));
            SQLite::Statement upsertPrinting(database, std::format(R"sql(
            INSERT INTO printings (
                scryfall_id, oracle_id, set_id, lang, collector_number, flavor_name,
                artist, frame, border_color, digital, full_art, oversized, promo,
                textless, reprint, rarity, image_small, image_normal
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(scryfall_id) {}
                oracle_id = excluded.oracle_id,
                set_id = excluded.set_id,
                lang = excluded.lang,
                collector_number = excluded.collector_number,
                flavor_name = excluded.flavor_name,
                artist = excluded.artist,
                frame = excluded.frame,
                border_color = excluded.border_color,
                digital = excluded.digital,
                full_art = excluded.full_art,
                oversized = excluded.oversized,
                promo = excluded.promo,
                textless = excluded.textless,
                reprint = excluded.reprint,
                rarity = excluded.rarity,
                image_small = excluded.image_small,
                image_normal = excluded.image_normal
        )sql", conflictProtocol));

            SQLite::Statement deleteFaces(database, "DELETE FROM card_faces WHERE card_id = ?");
            SQLite::Statement deleteKeywords(database, "DELETE FROM keywords WHERE card_id = ?");
            SQLite::Statement deleteLegalities(database, "DELETE FROM legalities WHERE card_id = ?");
            SQLite::Statement deleteFinishes(database, "DELETE FROM finishes WHERE printing_id = ?");
            SQLite::Statement deletePrintingFaces(database, "DELETE FROM printing_faces WHERE printing_id = ?");
            SQLite::Statement deleteFrameEffects(database, "DELETE FROM frame_effects WHERE printing_id = ?");
            SQLite::Statement deleteArtistIds(database, "DELETE FROM artist_ids WHERE printing_id = ?");

            SQLite::Statement insertFace(database, R"sql(
            INSERT OR IGNORE INTO card_faces (
                card_id, ordinal, name, artist, artist_id, colors, cmc, mana_cost,
                defense, defense_numeric, power, power_numeric, toughness,
                toughness_numeric, loyalty, loyalty_numeric, type_line, image_small, image_normal
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )sql");
            SQLite::Statement upsertPrintingFace(database, R"sql(
                INSERT INTO printing_faces(printing_id, ordinal, name, artist, artist_id, image_small, image_normal)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(printing_id, ordinal) DO UPDATE SET
                    name=excluded.name, artist=excluded.artist, artist_id=excluded.artist_id,
                    image_small=excluded.image_small, image_normal=excluded.image_normal
            )sql");
            SQLite::Statement insertKeyword(database, "INSERT OR IGNORE INTO keywords(card_id, keyword) VALUES (?, ?)");
            SQLite::Statement insertLegality(database,
                                             "INSERT OR IGNORE INTO legalities(card_id, format, legality) VALUES (?, ?, ?)");
            SQLite::Statement insertFinish(database,
                                           "INSERT OR IGNORE INTO finishes(printing_id, finish) VALUES (?, ?)");
            SQLite::Statement insertFrameEffect(database,
                                                "INSERT OR IGNORE INTO frame_effects(printing_id, effect) VALUES (?, ?)");
            SQLite::Statement insertArtistId(database,
                                             "INSERT OR IGNORE INTO artist_ids(printing_id, artist_id) VALUES (?, ?)");

            SQLite::Statement insertPromoType(
                database, "INSERT OR IGNORE INTO promo_types(printing_id, promo_type) VALUES (?, ?)");

            SQLite::Statement insertSupertype(
                database, "INSERT OR IGNORE INTO supertypes(card_id, supertype) VALUES (?, ?)");
            SQLite::Statement insertSubtype(
                database, "INSERT OR IGNORE INTO subtypes(card_id, subtype) VALUES (?, ?)");

            for (const Card &card: cards) {
                if (card.id.empty() || card.OracleId.empty() || card.Name.empty())
                    throw std::invalid_argument("Cannot store a card with an empty printing ID, oracle ID, or name");
                if (card.SetId.empty() || card.SetCode.empty() || card.SetName.empty() || card.SetType.empty())
                    throw std::invalid_argument("Cannot store a card with incomplete set metadata");
                if (card.CollectorNumber.empty() || card.Rarity.empty())
                    throw std::invalid_argument("Cannot store a printing without collector number and rarity");

                const std::string releasedAt = FormatDate(card.ReleasedAt);

                upsertCard.bind(1, card.OracleId);
                upsertCard.bind(2, card.Name);
                upsertCard.bind(3, card.Layout);
                upsertCard.bind(4, card.ManaCost);
                upsertCard.bind(5, static_cast<int>(card.ColorIdentity));
                upsertCard.bind(6, static_cast<int>(card.Colors));
                upsertCard.bind(7, static_cast<double>(card.cmc));
                upsertCard.bind(8, card.Typeline);
                BindNullableText(upsertCard, 9, card.Power);
                BindNullableNumeric(upsertCard, 10, card.Power);
                BindNullableText(upsertCard, 11, card.Toughness);
                BindNullableNumeric(upsertCard, 12, card.Toughness);
                BindNullableText(upsertCard, 13, card.Defense);
                BindNullableNumeric(upsertCard, 14, card.Defense);
                BindNullableText(upsertCard, 15, card.Loyalty);
                BindNullableNumeric(upsertCard, 16, card.Loyalty);
                BindNullableText(upsertCard, 17, card.OracleText);
                upsertCard.exec();
                Reset(upsertCard);

                // deleteFaces.bind(1, card.OracleId);
                // deleteFaces.exec();
                // Reset(deleteFaces);
                // deleteKeywords.bind(1, card.OracleId);
                // deleteKeywords.exec();
                // Reset(deleteKeywords);
                // deleteLegalities.bind(1, card.OracleId);
                // deleteLegalities.exec();
                // Reset(deleteLegalities);

                for (const CardFace &face: card.CardFaces) {
                    if (face.Name.empty())
                        throw std::invalid_argument("Cannot store a card face with an empty name");

                    insertFace.bind(1, card.OracleId);
                    insertFace.bind(2, static_cast<int>(face.Ordinal));
                    insertFace.bind(3, face.Name);
                    BindNullableText(insertFace, 4, face.Artist);
                    BindNullableText(insertFace, 5, face.ArtistId);
                    insertFace.bind(6, static_cast<int>(face.Colors));
                    insertFace.bind(7, static_cast<double>(face.Cmc));
                    BindNullableText(insertFace, 8, face.ManaCost);
                    BindNullableText(insertFace, 9, face.Defense);
                    BindNullableNumeric(insertFace, 10, face.Defense);
                    BindNullableText(insertFace, 11, face.Power);
                    BindNullableNumeric(insertFace, 12, face.Power);
                    BindNullableText(insertFace, 13, face.Toughness);
                    BindNullableNumeric(insertFace, 14, face.Toughness);
                    BindNullableText(insertFace, 15, face.Loyalty);
                    BindNullableNumeric(insertFace, 16, face.Loyalty);
                    BindNullableText(insertFace, 17, face.TypeLine);
                    BindNullableText(insertFace, 18, face.SmallImageURI);
                    BindNullableText(insertFace, 19, face.NormalImageURI);
                    insertFace.exec();
                    Reset(insertFace);
                }

                for (const auto &[format, legality]: card.Legalities) {
                    insertLegality.bind(1, card.OracleId);
                    insertLegality.bind(2, format);
                    insertLegality.bind(3, legality);
                    insertLegality.exec();
                    Reset(insertLegality);
                }

                for (const std::string &keyword: card.Keywords) {
                    if (keyword.empty())
                        continue;
                    insertKeyword.bind(1, card.OracleId);
                    insertKeyword.bind(2, keyword);
                    insertKeyword.exec();
                    Reset(insertKeyword);
                }

                upsertSet.bind(1, card.SetId);
                upsertSet.bind(2, card.SetCode);
                upsertSet.bind(3, card.SetName);
                upsertSet.bind(4, releasedAt);
                upsertSet.bind(5, card.SetType);
                upsertSet.exec();
                Reset(upsertSet);

                upsertPrinting.bind(1, card.id);
                upsertPrinting.bind(2, card.OracleId);
                upsertPrinting.bind(3, card.SetId);
                upsertPrinting.bind(4, card.Language);
                upsertPrinting.bind(5, card.CollectorNumber);
                BindNullableText(upsertPrinting, 6, card.FlavorName);
                BindNullableText(upsertPrinting, 7, card.Artist);
                BindNullableText(upsertPrinting, 8, card.Frame);
                BindNullableText(upsertPrinting, 9, card.BorderColor);
                upsertPrinting.bind(10, card.bDigital ? 1 : 0);
                upsertPrinting.bind(11, card.bFullArt ? 1 : 0);
                upsertPrinting.bind(12, card.bOversized ? 1 : 0);
                upsertPrinting.bind(13, card.bPromo ? 1 : 0);
                upsertPrinting.bind(14, card.bTextless ? 1 : 0);
                upsertPrinting.bind(15, card.bReprint ? 1 : 0);
                upsertPrinting.bind(16, card.Rarity);
                BindNullableText(upsertPrinting, 17, card.SmallImageURI);
                BindNullableText(upsertPrinting, 18, card.NormalImageURI);
                upsertPrinting.exec();
                Reset(upsertPrinting);

                deletePrintingFaces.bind(1, card.id);
                deletePrintingFaces.exec();
                Reset(deletePrintingFaces);
                for (const CardFace &face : card.CardFaces) {
                    upsertPrintingFace.bind(1, card.id);
                    upsertPrintingFace.bind(2, static_cast<int>(face.Ordinal));
                    upsertPrintingFace.bind(3, face.Name);
                    BindNullableText(upsertPrintingFace, 4, face.Artist);
                    BindNullableText(upsertPrintingFace, 5, face.ArtistId);
                    BindNullableText(upsertPrintingFace, 6, face.SmallImageURI);
                    BindNullableText(upsertPrintingFace, 7, face.NormalImageURI);
                    upsertPrintingFace.exec();
                    Reset(upsertPrintingFace);
                }

                // deleteFinishes.bind(1, card.id);
                // deleteFinishes.exec();
                // Reset(deleteFinishes);
                // deleteFrameEffects.bind(1, card.id);
                // deleteFrameEffects.exec();
                // Reset(deleteFrameEffects);
                // deleteArtistIds.bind(1, card.id);
                // deleteArtistIds.exec();
                // Reset(deleteArtistIds);

                for (const std::string &finish: card.Finishes) {
                    insertFinish.bind(1, card.id);
                    insertFinish.bind(2, finish);
                    insertFinish.exec();
                    Reset(insertFinish);
                }
                for (const std::string &effect: card.FrameEffects) {
                    insertFrameEffect.bind(1, card.id);
                    insertFrameEffect.bind(2, effect);
                    insertFrameEffect.exec();
                    Reset(insertFrameEffect);
                }
                for (const std::string &artistId: card.ArtistIds) {
                    insertArtistId.bind(1, card.id);
                    insertArtistId.bind(2, artistId);
                    insertArtistId.exec();
                    Reset(insertArtistId);
                }
                for (const std::string &promoType: card.PromoTypes) {
                    insertPromoType.bind(1, card.id);
                    insertPromoType.bind(2, promoType);
                    insertPromoType.exec();
                    Reset(insertPromoType);
                }
                for (const std::string &supertype: card.SuperTypes) {
                    insertSupertype.bind(1, card.OracleId);
                    insertSupertype.bind(2, supertype);
                    insertSupertype.exec();
                    Reset(insertSupertype);
                }
                for (const std::string &subtype: card.SubTypes) {
                    insertSubtype.bind(1, card.OracleId);
                    insertSubtype.bind(2, subtype);
                    insertSubtype.exec();
                    Reset(insertSubtype);
                }
            }

            transaction.commit();
        } catch (const std::exception &error) {
            throw std::runtime_error(std::string("Failed to store card batch in SQLite: ") + error.what());
        }
    }

    void SQLiteStorage::EmptyStorage() {
        try {
            SQLite::Database &database = impl_->database;
            SQLite::Transaction transaction(database);
            database.exec("DELETE FROM cards");
            database.exec("DELETE FROM sets");
            transaction.commit();
        } catch (const std::exception &error) {
            throw std::runtime_error(std::string("Failed to empty SQLite storage: ") + error.what());
        }
    }

    void SQLiteStorage::GetAllTrackedSets(std::map<std::string, std::uint64_t> &OUTTrackedSets) {
        try {
            SQLite::Statement query(impl_->database,
                "SELECT set_id, COUNT(*) FROM printings GROUP BY set_id ORDER BY set_id");
            std::map<std::string, std::uint64_t> results;
            while (query.executeStep()) {
                results.emplace(query.getColumn(0).getString(),
                                static_cast<std::uint64_t>(query.getColumn(1).getInt64()));
            }
            OUTTrackedSets = std::move(results);
        } catch (const std::exception &error) {
            throw std::runtime_error(std::string("Failed to read tracked sets from SQLite: ") + error.what());
        }
    }

    types::QueryPage SQLiteStorage::QueryForCards(const CardQuery &query, types::PageRequest page) {
        constexpr std::uint32_t MAX_PAGE_SIZE = 1000;
        if (page.Limit == 0 || page.Limit > MAX_PAGE_SIZE)
            throw std::invalid_argument("Query page limit must be between 1 and 1000");
        if (page.Offset > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
            throw std::invalid_argument("Query page offset is too large");

        std::string fromWhere = "FROM cards c JOIN printings p ON p.oracle_id=c.oracle_id WHERE 1=1";
        std::vector<QueryValue> values;
        if (!query.Name().empty()) {
            fromWhere += " AND (instr(lower(c.name),lower(?))>0 OR EXISTS "
                         "(SELECT 1 FROM card_faces f WHERE f.card_id=c.oracle_id AND instr(lower(f.name),lower(?))>0))";
            values.emplace_back(query.Name());
            values.emplace_back(query.Name());
        }
        if (!query.Sets().empty()) {
            std::string condition = "(";
            for (std::size_t i=0;i<query.Sets().size();++i) {
                if (i) condition += " OR ";
                condition += "EXISTS (SELECT 1 FROM sets sx WHERE sx.set_id=p.set_id AND (lower(sx.code)=lower(?) OR sx.set_id=?))";
                values.emplace_back(query.Sets()[i]); values.emplace_back(query.Sets()[i]);
            }
            fromWhere += " AND " + condition + ")";
        }
        AddNumericConditions(fromWhere, values, query.Power(), "c.power_numeric", "power_numeric");
        AddNumericConditions(fromWhere, values, query.Toughness(), "c.toughness_numeric", "toughness_numeric");
        AddNumericConditions(fromWhere, values, query.Loyalty(), "c.loyalty_numeric", "loyalty_numeric");
        AddNumericConditions(fromWhere, values, query.Defense(), "c.defense_numeric", "defense_numeric");
        AddNumericConditions(fromWhere, values, query.CMC(), "c.cmc", "cmc");

        if (query.HasColors()) {
            const auto [op, colors] = query.ColorsFilter();
            std::string colorType = query.IsColorIdentity() ? "color_identity" : "colors";

            if (op == Exactly) { fromWhere += std::format(" AND c.{}=?", colorType); values.emplace_back(static_cast<int>(colors)); }
            else if (op == AtMost) { fromWhere += std::format(" AND (c.{0} & ?)=c.{0}", colorType); values.emplace_back(static_cast<int>(colors)); }
            else if (op == Including) {
                fromWhere += std::format(" AND (c.{} & ?)=?", colorType);
                values.emplace_back(static_cast<int>(colors)); values.emplace_back(static_cast<int>(colors));
            } else throw std::invalid_argument("Invalid color operator");
        }
        for (const auto &[format, legality] : query.Legalities()) {
            if (format.empty()) throw std::invalid_argument("Legality format cannot be empty");
            std::string expected;
            switch (legality) {
                case LegalityOperators::Legal: expected="legal"; break;
                case LegalityOperators::Banned: expected="banned"; break;
                case LegalityOperators::NotLegal: expected="not_legal"; break;
                default: throw std::invalid_argument("Invalid legality operator");
            }
            fromWhere += " AND EXISTS(SELECT 1 FROM legalities l WHERE l.card_id=c.oracle_id AND l.format=? AND l.legality=?)";
            values.emplace_back(format); values.emplace_back(expected);
        }
        for (const auto &type : query.Supertypes()) {
            fromWhere += " AND EXISTS(SELECT 1 FROM supertypes st WHERE st.card_id=c.oracle_id AND st.supertype=?)";
            values.emplace_back(type);
        }
        for (const auto &type : query.Subtypes()) {
            fromWhere += " AND EXISTS(SELECT 1 FROM subtypes st WHERE st.card_id=c.oracle_id AND st.subtype=?)";
            values.emplace_back(type);
        }
        for (const auto &symbol : query.ManaSymbols()) {
            if (!symbol) throw std::invalid_argument("Mana symbol query cannot contain a null symbol");
            fromWhere += " AND (instr(c.mana_cost,?)>0 OR EXISTS(SELECT 1 FROM card_faces f "
                         "WHERE f.card_id=c.oracle_id AND instr(f.mana_cost,?)>0))";
            values.emplace_back(symbol->GetManaSymbolString()); values.emplace_back(symbol->GetManaSymbolString());
        }
        if (!query.Rarities().empty()) {
            fromWhere += " AND p.rarity IN (";
            for (std::size_t i=0;i<query.Rarities().size();++i) {
                if (i) fromWhere += ",";
                fromWhere += "?"; values.emplace_back(RarityText(query.Rarities()[i]));
            }
            fromWhere += ")";
        }

        std::string printingFilter;
        std::vector<QueryValue> printingValues;
        if (!query.Sets().empty()) {
            printingFilter += " AND (";
            for (std::size_t i=0;i<query.Sets().size();++i) {
                if (i) printingFilter += " OR ";
                printingFilter += "(lower(s.code)=lower(?) OR s.set_id=?)";
                printingValues.emplace_back(query.Sets()[i]); printingValues.emplace_back(query.Sets()[i]);
            }
            printingFilter += ")";
        }
        if (!query.Rarities().empty()) {
            printingFilter += " AND p.rarity IN (";
            for (std::size_t i=0;i<query.Rarities().size();++i) {
                if (i) printingFilter += ",";
                printingFilter += "?"; printingValues.emplace_back(RarityText(query.Rarities()[i]));
            }
            printingFilter += ")";
        }

        try {
            SQLite::Database &database = impl_->database;
            types::QueryPage result;
            result.Page = page;
            SQLite::Statement count(database, "SELECT COUNT(DISTINCT c.oracle_id) " + fromWhere);
            BindQueryValues(count, values);
            if (count.executeStep()) result.TotalCards = static_cast<std::uint64_t>(count.getColumn(0).getInt64());

            SQLite::Statement idsQuery(database,
                "SELECT oracle_id FROM (SELECT DISTINCT c.oracle_id,lower(c.name) AS sort_name " + fromWhere +
                ") matched_cards ORDER BY sort_name,oracle_id LIMIT ? OFFSET ?");
            BindQueryValues(idsQuery, values);
            const int pagingIndex = static_cast<int>(values.size()) + 1;
            idsQuery.bind(pagingIndex, static_cast<int>(page.Limit));
            idsQuery.bind(pagingIndex+1, static_cast<std::int64_t>(page.Offset));
            std::vector<std::string> ids;
            while (idsQuery.executeStep()) ids.push_back(idsQuery.getColumn(0).getString());

            SQLite::Statement oracleStmt(database,
                "SELECT oracle_id,name,layout,mana_cost,color_identity,colors,cmc,type_line,power,toughness,defense,loyalty,oracle_text "
                "FROM cards WHERE oracle_id=?");
            SQLite::Statement facesStmt(database,
                "SELECT ordinal,name,colors,cmc,mana_cost,defense,power,toughness,loyalty,type_line FROM card_faces WHERE card_id=? ORDER BY ordinal");
            SQLite::Statement keywordsStmt(database,"SELECT keyword FROM keywords WHERE card_id=? ORDER BY keyword");
            SQLite::Statement legalitiesStmt(database,"SELECT format,legality FROM legalities WHERE card_id=? ORDER BY format");
            SQLite::Statement supersStmt(database,"SELECT supertype FROM supertypes WHERE card_id=? ORDER BY supertype");
            SQLite::Statement subsStmt(database,"SELECT subtype FROM subtypes WHERE card_id=? ORDER BY subtype");
            SQLite::Statement printsStmt(database,
                "SELECT p.scryfall_id,p.lang,p.collector_number,p.flavor_name,p.artist,p.frame,p.border_color,"
                "p.digital,p.full_art,p.oversized,p.promo,p.textless,p.reprint,p.rarity,p.image_small,p.image_normal,"
                "s.set_id,s.code,s.name,s.set_type,s.released_at FROM printings p JOIN sets s ON s.set_id=p.set_id "
                "WHERE p.oracle_id=? " + printingFilter +
                " ORDER BY s.released_at,s.code,p.collector_number,p.scryfall_id");
            SQLite::Statement finishesStmt(database,"SELECT finish FROM finishes WHERE printing_id=? ORDER BY finish");
            SQLite::Statement effectsStmt(database,"SELECT effect FROM frame_effects WHERE printing_id=? ORDER BY effect");
            SQLite::Statement artistIdsStmt(database,"SELECT artist_id FROM artist_ids WHERE printing_id=? ORDER BY artist_id");
            SQLite::Statement promoStmt(database,"SELECT promo_type FROM promo_types WHERE printing_id=? ORDER BY promo_type");
            SQLite::Statement printingFacesStmt(database,
                "SELECT ordinal,name,artist,artist_id,image_small,image_normal FROM printing_faces WHERE printing_id=? ORDER BY ordinal");
            SQLite::Statement legacyFaceStmt(database,
                "SELECT ordinal,name,artist,artist_id,image_small,image_normal FROM card_faces WHERE card_id=? ORDER BY ordinal");

            auto loadStrings=[](SQLite::Statement &stmt,const std::string &key,auto add) {
                stmt.bind(1,key);
                while(stmt.executeStep()) add(stmt.getColumn(0).getString());
                Reset(stmt);
            };

            for (const auto &oracleId : ids) {
                oracleStmt.bind(1,oracleId);
                if (!oracleStmt.executeStep()) { Reset(oracleStmt); continue; }
                auto col=[&](int i){return oracleStmt.getColumn(i);};
                types::OracleCard card;
                card.OracleId=col(0).getString(); card.Name=col(1).getString(); card.Layout=col(2).getString();
                card.ManaCost=col(3).getString(); card.ColorIdentity=static_cast<Colors>(col(4).getInt());
                card.Colors=static_cast<Colors>(col(5).getInt()); card.Cmc=static_cast<float>(col(6).getDouble());
                card.TypeLine=col(7).getString(); card.Power=col(8).getString(); card.Toughness=col(9).getString();
                card.Defense=col(10).getString(); card.Loyalty=col(11).getString(); card.OracleText=col(12).getString();
                Reset(oracleStmt);

                facesStmt.bind(1,oracleId);
                while(facesStmt.executeStep()) {
                    auto fc=[&](int i){return facesStmt.getColumn(i);};
                    types::OracleFace face;
                    face.Ordinal=static_cast<std::uint8_t>(fc(0).getInt()); face.Name=fc(1).getString();
                    face.Colors=static_cast<Colors>(fc(2).getInt()); face.Cmc=static_cast<float>(fc(3).getDouble());
                    face.ManaCost=fc(4).getString(); face.Defense=fc(5).getString(); face.Power=fc(6).getString();
                    face.Toughness=fc(7).getString(); face.Loyalty=fc(8).getString(); face.TypeLine=fc(9).getString();
                    card.Faces.push_back(std::move(face));
                }
                Reset(facesStmt);
                loadStrings(keywordsStmt,oracleId,[&](std::string v){card.Keywords.push_back(std::move(v));});
                legalitiesStmt.bind(1,oracleId);
                while(legalitiesStmt.executeStep()) card.Legalities.emplace(legalitiesStmt.getColumn(0).getString(),legalitiesStmt.getColumn(1).getString());
                Reset(legalitiesStmt);
                loadStrings(supersStmt,oracleId,[&](std::string v){card.SuperTypes.insert(std::move(v));});
                loadStrings(subsStmt,oracleId,[&](std::string v){card.SubTypes.insert(std::move(v));});

                printsStmt.bind(1,oracleId);
                int filterIndex=2;
                for(const auto &v:printingValues) std::visit([&](const auto &item){printsStmt.bind(filterIndex++,item);},v);
                while(printsStmt.executeStep()) {
                    auto pc=[&](int i){return printsStmt.getColumn(i);};
                    types::Printing p;
                    p.Id=pc(0).getString(); p.Language=pc(1).getString(); p.CollectorNumber=pc(2).getString();
                    p.FlavorName=pc(3).getString(); p.Artist=pc(4).getString(); p.Frame=pc(5).getString();
                    p.BorderColor=pc(6).getString(); p.Digital=pc(7).getInt()!=0; p.FullArt=pc(8).getInt()!=0;
                    p.Oversized=pc(9).getInt()!=0; p.Promo=pc(10).getInt()!=0; p.Textless=pc(11).getInt()!=0;
                    p.Reprint=pc(12).getInt()!=0; p.Rarity=pc(13).getString(); p.SmallImageURI=pc(14).getString();
                    p.NormalImageURI=pc(15).getString(); p.SetId=pc(16).getString(); p.SetCode=pc(17).getString();
                    p.SetName=pc(18).getString(); p.SetType=pc(19).getString();
                    const std::string date=pc(20).getString();
                    if(date.size()==10) p.ReleasedAt=std::chrono::year{std::stoi(date.substr(0,4))} /
                        std::chrono::month{static_cast<unsigned>(std::stoi(date.substr(5,2)))} /
                        std::chrono::day{static_cast<unsigned>(std::stoi(date.substr(8,2)))};
                    loadStrings(finishesStmt,p.Id,[&](std::string v){p.Finishes.push_back(std::move(v));});
                    loadStrings(effectsStmt,p.Id,[&](std::string v){p.FrameEffects.push_back(std::move(v));});
                    loadStrings(artistIdsStmt,p.Id,[&](std::string v){p.ArtistIds.push_back(std::move(v));});
                    loadStrings(promoStmt,p.Id,[&](std::string v){p.PromoTypes.insert(std::move(v));});
                    printingFacesStmt.bind(1,p.Id);
                    while(printingFacesStmt.executeStep()) {
                        auto fc=[&](int i){return printingFacesStmt.getColumn(i);};
                        types::PrintingFace f; f.Ordinal=static_cast<std::uint8_t>(fc(0).getInt());
                        f.Name=fc(1).getString(); f.Artist=fc(2).getString(); f.ArtistId=fc(3).getString();
                        f.SmallImageURI=fc(4).getString(); f.NormalImageURI=fc(5).getString();
                        p.Faces.push_back(std::move(f));
                    }
                    Reset(printingFacesStmt);
                    if(p.Faces.empty()) {
                        legacyFaceStmt.bind(1,oracleId);
                        while(legacyFaceStmt.executeStep()) {
                            auto fc=[&](int i){return legacyFaceStmt.getColumn(i);};
                            types::PrintingFace f; f.Ordinal=static_cast<std::uint8_t>(fc(0).getInt());
                            f.Name=fc(1).getString(); f.Artist=fc(2).getString(); f.ArtistId=fc(3).getString();
                            f.SmallImageURI=fc(4).getString(); f.NormalImageURI=fc(5).getString();
                            p.Faces.push_back(std::move(f));
                        }
                        Reset(legacyFaceStmt);
                    }
                    card.Printings.push_back(std::move(p));
                }
                Reset(printsStmt);
                result.Cards.push_back(std::move(card));
            }
            return result;
        } catch(const std::exception &error) {
            throw std::runtime_error(std::string("Failed to query cards from SQLite: ")+error.what());
        }
    }
}

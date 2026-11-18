#include "api/api_interface.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include "storage/sqlite/sqlite_storage.h"

namespace {
    using json = nlohmann::json;
    using namespace magindex::types;

    thread_local std::string lastOpenError;

    class JsonManaSymbol final : public ManaSymbol {
    public:
        explicit JsonManaSymbol(std::string value) : value_(std::move(value)) {}
        std::string GetManaSymbolString() override { return value_; }
    private:
        std::string value_;
    };

    std::string Lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    std::string ManaToken(std::string value) {
        value = Lower(std::move(value));
        if (value.size() >= 2 && value.front() == '{' && value.back() == '}')
            value = value.substr(1, value.size() - 2);
        if (value.empty()) throw std::invalid_argument("mana_symbols entries cannot be empty");
        for (char &ch : value) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return "{" + value + "}";
    }

    EqualityOperators ParseComparison(const std::string &value) {
        if (value == ">") return GreaterThan;
        if (value == ">=") return GreaterThanOrEqualTo;
        if (value == "<") return LessThan;
        if (value == "<=") return LessThanOrEqualTo;
        if (value == "=" || value == "==") return EqualTo;
        throw std::invalid_argument("Numeric operator must be >, >=, <, <=, or =");
    }

    LogicalOperators ParseLogic(const json &item) {
        const std::string value = Lower(item.value("logic", std::string("and")));
        if (value == "and") return And;
        if (value == "or") return Or;
        throw std::invalid_argument("Numeric logic must be 'and' or 'or'");
    }

    void ParseNumericalList(const json &root, const char *key,
                            const std::function<void(EqualityOperators, std::int32_t, LogicalOperators)> &add) {
        const auto found = root.find(key);
        if (found == root.end()) return;
        if (!found->is_array()) throw std::invalid_argument(std::string("'") + key + "' must be an array");
        for (const auto &item : *found) {
            if (!item.is_object() || !item.contains("op") || !item["op"].is_string() ||
                !item.contains("value") || !item["value"].is_number_integer())
                throw std::invalid_argument(std::string("Each '") + key + "' entry needs string 'op' and integer 'value'");
            add(ParseComparison(item["op"].get<std::string>()), item["value"].get<std::int32_t>(), ParseLogic(item));
        }
    }

    CardQuery ParseQuery(const json &root) {
        if (!root.is_object()) throw std::invalid_argument("Query JSON must be an object");
        CardQuery query;

        if (root.contains("name")) {
            if (!root["name"].is_string()) throw std::invalid_argument("'name' must be a string");
            query.NameParam(root["name"].get<std::string>());
        }
        const auto parseStrings = [&](const char *key, auto setter) {
            const auto found = root.find(key);
            if (found == root.end()) return;
            if (!found->is_array()) throw std::invalid_argument(std::string("'") + key + "' must be an array of strings");
            std::vector<std::string> values;
            for (const auto &item : *found) {
                if (!item.is_string()) throw std::invalid_argument(std::string("'") + key + "' entries must be strings");
                values.push_back(item.get<std::string>());
            }
            setter(std::move(values));
        };
        parseStrings("sets", [&](auto values) { query.SetsParam(std::move(values)); });
        parseStrings("supertypes", [&](auto values) { query.SupertypesParam(std::move(values)); });
        parseStrings("subtypes", [&](auto values) { query.SubtypesParam(std::move(values)); });

        ParseNumericalList(root, "power", [&](auto op, auto value, auto logic) { query.PowerParam(op, value, logic); });
        ParseNumericalList(root, "toughness", [&](auto op, auto value, auto logic) { query.ToughnessParam(op, value, logic); });
        ParseNumericalList(root, "loyalty", [&](auto op, auto value, auto logic) { query.LoyaltyParam(op, value, logic); });
        ParseNumericalList(root, "defense", [&](auto op, auto value, auto logic) { query.DefenseParam(op, value, logic); });
        ParseNumericalList(root, "cmc", [&](auto op, auto value, auto logic) { query.CMCParam(op, value, logic); });

        if (root.contains("colors")) {
            const auto &filter = root["colors"];
            if (!filter.is_object() || !filter.contains("op") || !filter["op"].is_string() ||
                !filter.contains("colors") || !filter["colors"].is_array())
                throw std::invalid_argument("'colors' needs string 'op' and array 'colors'");
            const std::string op = Lower(filter["op"].get<std::string>());
            ColorOperators colorOp;
            if (op == "exactly") colorOp = Exactly;
            else if (op == "at_most") colorOp = AtMost;
            else if (op == "including") colorOp = Including;
            else throw std::invalid_argument("Color op must be 'exactly', 'at_most', or 'including'");
            Colors colors = NA;
            for (const auto &item : filter["colors"]) {
                if (!item.is_string() || item.get<std::string>().size() != 1)
                    throw std::invalid_argument("Color entries must be one-letter strings C/W/U/B/R/G");
                colors |= static_cast<Colors>(CharToColor(static_cast<char>(std::toupper(
                    static_cast<unsigned char>(item.get<std::string>()[0])))));
            }
            query.ColorsParam(colorOp, colors);
        }

        if (root.contains("legalities")) {
            if (!root["legalities"].is_object()) throw std::invalid_argument("'legalities' must be an object of format to status");
            std::map<std::string, LegalityOperators> legalities;
            for (auto it = root["legalities"].begin(); it != root["legalities"].end(); ++it) {
                if (!it.value().is_string()) throw std::invalid_argument("Legality values must be strings");
                const std::string value = Lower(it.value().get<std::string>());
                if (value == "legal") legalities.emplace(it.key(), Legal);
                else if (value == "banned") legalities.emplace(it.key(), Banned);
                else if (value == "not_legal" || value == "not legal") legalities.emplace(it.key(), NotLegal);
                else throw std::invalid_argument("Legality must be 'legal', 'banned', or 'not_legal'");
            }
            query.LegalitiesParam(std::move(legalities));
        }

        if (root.contains("mana_symbols")) {
            if (!root["mana_symbols"].is_array()) throw std::invalid_argument("'mana_symbols' must be an array of tokens");
            std::vector<std::shared_ptr<ManaSymbol>> symbols;
            for (const auto &item : root["mana_symbols"]) {
                if (!item.is_string()) throw std::invalid_argument("Mana symbols must be strings");
                symbols.push_back(std::make_shared<JsonManaSymbol>(ManaToken(item.get<std::string>())));
            }
            query.ManaCostParam(std::move(symbols));
        }

        if (root.contains("rarities")) {
            if (!root["rarities"].is_array()) throw std::invalid_argument("'rarities' must be an array");
            for (const auto &item : root["rarities"]) {
                if (!item.is_string()) throw std::invalid_argument("Rarity values must be strings");
                const std::string value = Lower(item.get<std::string>());
                if (value == "common") query.RarityParam(Common);
                else if (value == "uncommon") query.RarityParam(Uncommon);
                else if (value == "rare") query.RarityParam(Rare);
                else if (value == "mythic") query.RarityParam(Mythic);
                else throw std::invalid_argument("Rarity must be common, uncommon, rare, or mythic");
            }
        }
        return query;
    }

    json ColorsJson(Colors colors) {
        json result = json::array();
        constexpr std::pair<Colors, const char *> entries[] = {
            {Colorless, "C"}, {White, "W"}, {Blue, "U"}, {Black, "B"}, {Red, "R"}, {Green, "G"}
        };
        for (const auto &[mask, code] : entries) if ((colors & mask) != 0) result.push_back(code);
        return result;
    }

    json CardJson(const Card &card) {
        json faces = json::array();
        for (const auto &face : card.CardFaces) {
            faces.push_back({{"name", face.Name}, {"ordinal", face.Ordinal}, {"oracle_id", face.OracleId},
                {"artist", face.Artist}, {"artist_id", face.ArtistId}, {"cmc", face.Cmc},
                {"colors", ColorsJson(face.Colors)}, {"supertypes", face.SuperTypes}, {"subtypes", face.SubTypes},
                {"mana_cost", face.ManaCost}, {"defense", face.Defense}, {"power", face.Power},
                {"toughness", face.Toughness}, {"loyalty", face.Loyalty}, {"type_line", face.TypeLine},
                {"image_small", face.SmallImageURI}, {"image_normal", face.NormalImageURI}});
        }
        const auto date = card.ReleasedAt;
        std::string releasedAt;
        if (date.ok()) {
            releasedAt = std::to_string(static_cast<int>(date.year())) + "-" +
                (static_cast<unsigned>(date.month()) < 10 ? "0" : "") + std::to_string(static_cast<unsigned>(date.month())) + "-" +
                (static_cast<unsigned>(date.day()) < 10 ? "0" : "") + std::to_string(static_cast<unsigned>(date.day()));
        }
        return {{"id", card.id}, {"oracle_id", card.OracleId}, {"language", card.Language}, {"layout", card.Layout},
            {"name", card.Name}, {"cmc", card.cmc}, {"colors", ColorsJson(card.Colors)},
            {"color_identity", ColorsJson(card.ColorIdentity)}, {"power", card.Power}, {"toughness", card.Toughness},
            {"defense", card.Defense}, {"loyalty", card.Loyalty}, {"mana_cost", card.ManaCost},
            {"type_line", card.Typeline}, {"keywords", card.Keywords}, {"legalities", card.Legalities},
            {"oracle_text", card.OracleText}, {"supertypes", card.SuperTypes}, {"subtypes", card.SubTypes},
            {"faces", std::move(faces)}, {"artist", card.Artist}, {"artist_ids", card.ArtistIds},
            {"border_color", card.BorderColor}, {"collector_number", card.CollectorNumber}, {"digital", card.bDigital},
            {"finishes", card.Finishes}, {"flavor_name", card.FlavorName}, {"frame", card.Frame},
            {"frame_effects", card.FrameEffects}, {"promo_types", card.PromoTypes}, {"full_art", card.bFullArt},
            {"image_small", card.SmallImageURI}, {"image_normal", card.NormalImageURI},
            {"oversized", card.bOversized}, {"promo", card.bPromo}, {"rarity", card.Rarity},
            {"released_at", releasedAt}, {"reprint", card.bReprint}, {"set_id", card.SetId},
            {"set_code", card.SetCode}, {"set_name", card.SetName}, {"set_type", card.SetType}, {"textless", card.bTextless}};
    }

    MagindexStatus StatusForException(const std::exception &error) {
        if (dynamic_cast<const std::invalid_argument *>(&error)) return MAGINDEX_STATUS_INVALID_ARGUMENT;
        if (dynamic_cast<const SQLite::Exception *>(&error)) return MAGINDEX_STATUS_DATABASE_ERROR;
        if (dynamic_cast<const std::runtime_error *>(&error)) return MAGINDEX_STATUS_DATABASE_ERROR;
        return MAGINDEX_STATUS_INTERNAL_ERROR;
    }
}

namespace magindex::api {
    class Context::Impl {
    public:
        explicit Impl(const std::filesystem::path &databasePath) : storage(databasePath) {}
        storage::SQLiteStorage storage;
        std::mutex mutex;
    };

    Context::Context(const std::filesystem::path &databasePath) : impl_(std::make_unique<Impl>(databasePath)) {}
    Context::~Context() = default;

    std::vector<types::Card> Context::Query(const types::CardQuery &query) {
        std::lock_guard lock(impl_->mutex);
        std::vector<types::Card> cards;
        impl_->storage.QueryForCards(query, cards);
        return cards;
    }

    void Context::Clear() {
        std::lock_guard lock(impl_->mutex);
        impl_->storage.EmptyStorage();
    }
}

struct MagindexHandle {
    explicit MagindexHandle(const std::filesystem::path &path) : context(path) {}
    magindex::api::Context context;
    std::mutex mutex;
    std::string lastError;
};

extern "C" MagindexStatus magindex_open(const char *database_path_utf8, MagindexHandle **out_handle) {
    if (out_handle) *out_handle = nullptr;
    if (!database_path_utf8 || !out_handle || database_path_utf8[0] == '\0') {
        lastOpenError = "Database path and output handle are required";
        return MAGINDEX_STATUS_INVALID_ARGUMENT;
    }
    try {
        *out_handle = new MagindexHandle(std::filesystem::u8path(database_path_utf8));
        lastOpenError.clear();
        return MAGINDEX_STATUS_OK;
    } catch (const std::exception &error) {
        lastOpenError = error.what();
        return StatusForException(error);
    } catch (...) {
        lastOpenError = "Unknown error while opening Magindex";
        return MAGINDEX_STATUS_INTERNAL_ERROR;
    }
}

extern "C" void magindex_close(MagindexHandle *handle) {
    delete handle;
}

extern "C" MagindexStatus magindex_clear(MagindexHandle *handle) {
    if (!handle) return MAGINDEX_STATUS_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(handle->mutex);
        handle->context.Clear();
        handle->lastError.clear();
        return MAGINDEX_STATUS_OK;
    } catch (const std::exception &error) {
        std::lock_guard lock(handle->mutex);
        handle->lastError = error.what();
        return StatusForException(error);
    } catch (...) {
        std::lock_guard lock(handle->mutex);
        handle->lastError = "Unknown error while clearing Magindex storage";
        return MAGINDEX_STATUS_INTERNAL_ERROR;
    }
}

extern "C" MagindexStatus magindex_query_json(MagindexHandle *handle, const char *query_json,
                                                char **out_result_json, size_t *out_result_size) {
    if (out_result_json) *out_result_json = nullptr;
    if (out_result_size) *out_result_size = 0;
    if (!handle || !query_json || !out_result_json || !out_result_size) return MAGINDEX_STATUS_INVALID_ARGUMENT;
    try {
        const json parsed = json::parse(query_json);
        const CardQuery query = ParseQuery(parsed);
        std::lock_guard lock(handle->mutex);
        const auto cards = handle->context.Query(query);
        json result = json::array();
        for (const auto &card : cards) result.push_back(CardJson(card));
        const std::string output = result.dump();
        auto *buffer = static_cast<char *>(std::malloc(output.size() + 1));
        if (!buffer) throw std::bad_alloc();
        std::memcpy(buffer, output.data(), output.size());
        buffer[output.size()] = '\0';
        *out_result_json = buffer;
        *out_result_size = output.size();
        handle->lastError.clear();
        return MAGINDEX_STATUS_OK;
    } catch (const std::exception &error) {
        std::lock_guard lock(handle->mutex);
        handle->lastError = error.what();
        if (dynamic_cast<const nlohmann::json::exception *>(&error)) return MAGINDEX_STATUS_QUERY_ERROR;
        return StatusForException(error);
    } catch (...) {
        std::lock_guard lock(handle->mutex);
        handle->lastError = "Unknown error while querying Magindex";
        return MAGINDEX_STATUS_INTERNAL_ERROR;
    }
}

extern "C" void magindex_free_string(char *value) {
    std::free(value);
}

extern "C" MagindexStatus magindex_get_last_error(MagindexHandle *handle, char *buffer,
                                                   size_t capacity, size_t *required_size) {
    if (!required_size) return MAGINDEX_STATUS_INVALID_ARGUMENT;
    std::string error;
    if (!handle) error = lastOpenError;
    else {
        std::lock_guard lock(handle->mutex);
        error = handle->lastError;
    }
    *required_size = error.size() + 1;
    if (!buffer && capacity == 0) return MAGINDEX_STATUS_OK;
    if (!buffer || capacity < *required_size) return MAGINDEX_STATUS_INVALID_ARGUMENT;
    std::memcpy(buffer, error.c_str(), *required_size);
    return MAGINDEX_STATUS_OK;
}

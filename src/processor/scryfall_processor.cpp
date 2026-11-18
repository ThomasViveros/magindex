#include "processor/scryfall_processor.h"

#include <array>
#include <ranges>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include <zlib.h>

#include "types/color.h"

namespace {
    using json = nlohmann::json;
    using namespace magindex::types;

    constexpr std::size_t INITIAL_CARD_VECTOR_ALLOCATION = 150000;

    class GzipInput {
    public:
        explicit GzipInput(const std::filesystem::path &path)
            : file_(gzopen(path.string().c_str(), "rb")) {
        }

        GzipInput(const GzipInput &) = delete;

        GzipInput &operator=(const GzipInput &) = delete;

        ~GzipInput() {
            if (file_)
                gzclose(file_);
        }

        [[nodiscard]] gzFile get() const noexcept { return file_; }

        int Close() noexcept {
            if (!file_)
                return Z_OK;
            const int result = gzclose(file_);
            file_ = nullptr;
            return result;
        }

    private:
        gzFile file_ = nullptr;
    };

    std::string OptionalString(const json &object, const char *field) {
        const auto it = object.find(field);
        if (it == object.end() || it->is_null())
            return {};
        if (!it->is_string())
            throw std::runtime_error(std::string("Field '") + field + "' must be a string or null");
        return it->get<std::string>();
    }

    std::string RequiredString(const json &object, const char *field) {
        const auto it = object.find(field);
        if (it == object.end() || !it->is_string())
            throw std::runtime_error(std::string("Required field '") + field + "' is missing or is not a string");
        std::string value = it->get<std::string>();
        if (value.empty())
            throw std::runtime_error(std::string("Required field '") + field + "' is empty");
        return value;
    }

    std::vector<std::string> StringArray(const json &object, const char *field) {
        const auto it = object.find(field);
        if (it == object.end() || it->is_null())
            return {};
        if (!it->is_array())
            throw std::runtime_error(std::string("Field '") + field + "' must be an array or null");

        std::vector<std::string> values;
        values.reserve(it->size());
        for (const auto &item: *it) {
            if (!item.is_string())
                throw std::runtime_error(std::string("Every item in '") + field + "' must be a string");
            values.push_back(item.get<std::string>());
        }
        return values;
    }

    Colors ParseColors(const json &object, const char *field) {
        Colors result = Color::NA;

        for (const std::string &color: StringArray(object, field)) {
            if (color.size() != 1)
                throw std::runtime_error("Every color entry must be a one-character color code");

            Color colorMask = CharToColor(color.front());
            result |= colorMask;
        }
        return result;
    }

    float OptionalCmc(const json &object) {
        const auto it = object.find("cmc");
        if (it == object.end() || it->is_null())
            return 0.0F;
        if (!it->is_number())
            throw std::runtime_error("Field 'cmc' must be numeric or null");
        return it->get<float>();
    }

    std::string SmallImageUri(const json &object) {
        const auto it = object.find("image_uris");
        if (it == object.end() || it->is_null())
            return "";
        if (!it->is_object()) {
            throw std::runtime_error("image_uris must be an object");
        }

        return OptionalString(*it, "small");
    }

    std::string NormalImageUri(const json &object) {
        const auto it = object.find("image_uris");
        if (it == object.end() || it->is_null())
            return "";
        if (!it->is_object()) {
            throw std::runtime_error("image_uris must be an object");
        }

        return OptionalString(*it, "normal");
    }

    void ParseTypes(const json &object, std::set<std::string> &OUTSuperTypes, std::set<std::string> &OUTSubTypes) {
        std::string typeLine = OptionalString(object, "type_line");

        if(typeLine.empty()){
            return;
        }

        const std::string separator = "\xE2\x80\x94"; // UTF-8 em dash
        const auto dash = typeLine.find(separator);

        const std::string supertypes =
            dash == std::string::npos ? typeLine : typeLine.substr(0, dash);
        const std::string subtypes =
            dash == std::string::npos ? "" : typeLine.substr(dash + separator.size());

        auto addWords = [](const std::string& text, auto& output) {
            std::istringstream words(text);
            for (std::string word; words >> word; ) {
                output.insert(word);
            }
        };

        addWords(supertypes, OUTSuperTypes);
        addWords(subtypes, OUTSubTypes);
    }

    std::vector<CardFace> CardFaces(const json &object) {
        const auto it = object.find("card_faces");
        if (it == object.end() || it->is_null())
            return {};
        if (!it->is_array())
            throw std::runtime_error("Field 'card_faces' must be an array or null");

        std::vector<CardFace> faces;
        faces.reserve(it->size());

        int index = 0;
        for (const auto &face: *it) {
            if (!face.is_object())
                throw std::runtime_error("Every item in 'card_faces' must be an object");

            CardFace newCardFace{};
            newCardFace.Name = OptionalString(face, "name");
            newCardFace.Ordinal = index++;
            newCardFace.OracleId = OptionalString(face, "oracle_id");
            newCardFace.Artist = OptionalString(face, "artist");
            newCardFace.ArtistId = OptionalString(face, "artist_id");
            newCardFace.Cmc = OptionalCmc(face);
            newCardFace.Colors = ParseColors(face, "colors");
            newCardFace.ManaCost = OptionalString(face, "mana_cost");
            newCardFace.Defense = OptionalString(face, "defense");
            newCardFace.Power = OptionalString(face, "power");
            newCardFace.Toughness = OptionalString(face, "toughness");
            newCardFace.Loyalty = OptionalString(face, "loyalty");
            newCardFace.TypeLine = OptionalString(face, "type_line");
            newCardFace.SmallImageURI = SmallImageUri(face);
            newCardFace.NormalImageURI = NormalImageUri(face);
            ParseTypes(object, newCardFace.SuperTypes, newCardFace.SubTypes);
            faces.push_back(newCardFace);
        }
        return faces;
    }

    bool OptionalBool(const json &object, const char *field) {
        const auto it = object.find(field);
        if (it == object.end() || it->is_null())
            return false;
        if (!it->is_boolean())
            throw std::runtime_error(std::string("Field '") + field + "' must be a boolean or null");
        return it->get<bool>();
    }

    std::chrono::year_month_day RequiredDate(const json &object, const char *field) {
        const auto it = object.find(field);
        if (it == object.end() || !it->is_string())
            throw std::runtime_error(std::string("Required field '") + field + "' is missing or is not a string");
        std::string value = it->get<std::string>();
        if (value.empty())
            throw std::runtime_error(std::string("Required field '") + field + "' is empty");

        if (value.length() != 10)
            throw std::runtime_error("Expected length of date to be 10");

        std::istringstream stream(value);
        std::chrono::year_month_day date;

        stream >> std::chrono::parse("%F", date);

        if (stream.fail() || !date.ok())
            throw std::runtime_error(std::string("Invalid date in '") + field + "'");

        return date;
    }

    std::map<std::string, std::string> Legalities(const json &object) {
        std::map<std::string, std::string> result;

        const auto it = object.find("legalities");
        if (it == object.end() || !it->is_object())
            throw std::runtime_error("Legalities must be present and an object");
        for (auto &[key, value]: it->items()) {
            result.emplace(key, value);
        }

        return result;
    }

    Card ParseCard(const json &object) {
        if (!object.is_object())
            throw std::runtime_error("Each JSONL record must be a card object");

        Card card{};
        card.id = RequiredString(object, "id");
        //The oracle id will always be present except for reversible_card layout. In that case there will be two found in card_faces
        card.OracleId = OptionalString(object, "oracle_id");
        card.Name = RequiredString(object, "name");
        card.Language = RequiredString(object, "lang");
        card.Layout = OptionalString(object, "layout");
        card.CardFaces = CardFaces(object);
        card.cmc = OptionalCmc(object);
        card.Colors = ParseColors(object, "colors");
        card.ColorIdentity = ParseColors(object, "color_identity");
        card.Layout = RequiredString(object, "layout");
        card.Power = OptionalString(object, "power");
        card.Toughness = OptionalString(object, "toughness");
        card.Defense = OptionalString(object, "defense");
        card.Loyalty = OptionalString(object, "loyalty");
        card.ManaCost = OptionalString(object, "mana_cost");
        card.Typeline = OptionalString(object, "type_line");
        card.Keywords = StringArray(object, "keywords");
        card.Legalities = Legalities(object);
        card.ReleasedAt = RequiredDate(object, "released_at");
        card.SmallImageURI = SmallImageUri(object);
        card.NormalImageURI = NormalImageUri(object);
        card.OracleText = OptionalString(object, "oracle_text");
        card.Artist = OptionalString(object, "artist");
        card.ArtistIds = StringArray(object, "artist_ids");
        card.BorderColor = RequiredString(object, "border_color");
        card.CollectorNumber = RequiredString(object, "collector_number");
        card.bDigital = OptionalBool(object, "digital");
        card.bFullArt = OptionalBool(object, "full_art");
        card.bPromo = OptionalBool(object, "promo");
        card.bReprint = OptionalBool(object, "reprint");
        card.bOversized = OptionalBool(object, "oversized");
        card.Finishes = StringArray(object, "finishes");
        card.FlavorName = OptionalString(object, "flavor_name");
        card.Frame = OptionalString(object, "frame");
        card.FrameEffects = StringArray(object, "frame_effects");
        card.SetId = OptionalString(object, "set_id");
        card.SetCode = OptionalString(object, "set");
        card.SetName = OptionalString(object, "set_name");
        card.SetType = OptionalString(object, "set_type");
        card.bTextless = OptionalBool(object, "textless");
        card.Rarity = RequiredString(object, "rarity");
        ParseTypes(object, card.SuperTypes, card.SubTypes);

        //Handle edge case where oracle id is not present in reversible_card layout
        if (card.OracleId.empty()) {
            if (card.CardFaces.empty())
                throw std::runtime_error("No oracle id given and card faces was empty");
            card.OracleId = card.CardFaces[0].OracleId;
        }

        return card;
    }

    void ParseLine(const std::filesystem::path &path,
                   std::size_t lineNumber,
                   std::string line,
                   std::vector<Card> &cards) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            return;

        try {
            cards.push_back(ParseCard(json::parse(line)));
        } catch (const std::exception &error) {
            throw std::runtime_error(path.string() + ": line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }

    void ReadPlainJsonl(const std::filesystem::path &path, std::vector<Card> &cards) {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Unable to open card file: " + path.string());

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            ParseLine(path, lineNumber, std::move(line), cards);
        }
        if (input.bad())
            throw std::runtime_error("I/O error while reading card file: " + path.string());
    }

    void ReadGzipJsonl(const std::filesystem::path &path, std::vector<Card> &cards) {
        GzipInput input(path);
        if (!input.get())
            throw std::runtime_error("Unable to open gzip card file: " + path.string());

        std::array<char, 64 * 1024> buffer{};
        std::string pending;
        std::size_t lineNumber = 0;

        while (true) {
            const int bytesRead = gzread(input.get(), buffer.data(), static_cast<unsigned>(buffer.size()));
            if (bytesRead < 0) {
                int errorCode = Z_OK;
                const char *errorText = gzerror(input.get(), &errorCode);
                throw std::runtime_error("Gzip read failed for " + path.string() + ": " +
                                         (errorText ? errorText : "unknown zlib error"));
            }
            if (bytesRead == 0) {
                int errorCode = Z_OK;
                const char *errorText = gzerror(input.get(), &errorCode);
                if (errorCode != Z_OK && errorCode != Z_STREAM_END)
                    throw std::runtime_error("Gzip read failed for " + path.string() + ": " +
                                             (errorText ? errorText : "unknown zlib error"));
                break;
            }

            pending.append(buffer.data(), static_cast<std::size_t>(bytesRead));
            std::size_t lineStart = 0;
            std::size_t newline = 0;
            while ((newline = pending.find('\n', lineStart)) != std::string::npos) {
                ++lineNumber;
                ParseLine(path, lineNumber, pending.substr(lineStart, newline - lineStart), cards);
                lineStart = newline + 1;
            }
            if (lineStart > 0)
                pending.erase(0, lineStart);
        }

        if (!pending.empty()) {
            ++lineNumber;
            ParseLine(path, lineNumber, std::move(pending), cards);
        }

        if (input.Close() != Z_OK)
            throw std::runtime_error("Failed to close gzip card file cleanly: " + path.string());
    }

    bool IsGzipFile(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Unable to open card file: " + path.string());
        unsigned char signature[2]{};
        input.read(reinterpret_cast<char *>(signature), sizeof(signature));
        return input.gcount() == 2 && signature[0] == 0x1f && signature[1] == 0x8b;
    }
}

namespace magindex::processor {

std::vector<Card> ScryfallProcessor::Process(std::filesystem::path file, UpdateType updateType,
                                             const MagindexConfig &config) {
    switch (updateType) {
        case UpdateType::None:
            return {};
        case UpdateType::Incremental:
        case UpdateType::IncrementalUsingBulk:
        case UpdateType::TotalBulk:
            break;
        default:
            throw std::invalid_argument("Unsupported update type");
    }

    std::vector<Card> cards;
    if (updateType != UpdateType::Incremental)
        cards.reserve(INITIAL_CARD_VECTOR_ALLOCATION);
    if (IsGzipFile(file))
        ReadGzipJsonl(file, cards);
    else
        ReadPlainJsonl(file, cards);
    return cards;
}

}
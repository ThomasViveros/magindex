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
#include "types/oracle_card.h"

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

    void CardFaces(const json &object, std::vector<OracleFace>& OUTCardFaces, std::vector<PrintingFace>& OUTPrintingFaces) {
        const auto it = object.find("card_faces");
        if (it == object.end() || it->is_null())
            return;
        if (!it->is_array())
            throw std::runtime_error("Field 'card_faces' must be an array or null");

        OUTCardFaces.reserve(it->size());
        OUTPrintingFaces.reserve(it->size());

        int index = 0;
        for (const auto &face: *it) {
            if (!face.is_object())
                throw std::runtime_error("Every item in 'card_faces' must be an object");

            OracleFace& newCardFace = OUTCardFaces.emplace_back();
            newCardFace.Name = OptionalString(face, "name");
            newCardFace.Ordinal = index++;
            newCardFace.OracleId = OptionalString(face, "oracle_id");
            newCardFace.Cmc = OptionalCmc(face);
            newCardFace.Colors = ParseColors(face, "colors");
            newCardFace.ManaCost = OptionalString(face, "mana_cost");
            newCardFace.Defense = OptionalString(face, "defense");
            newCardFace.Power = OptionalString(face, "power");
            newCardFace.Toughness = OptionalString(face, "toughness");
            newCardFace.Loyalty = OptionalString(face, "loyalty");
            newCardFace.TypeLine = OptionalString(face, "type_line");
            ParseTypes(object, newCardFace.SuperTypes, newCardFace.SubTypes);

            PrintingFace& newPrintingFace = OUTPrintingFaces.emplace_back();
            newPrintingFace.Artist = OptionalString(face, "artist");
            newPrintingFace.ArtistId = OptionalString(face, "artist_id");
            newPrintingFace.SmallImageURI = SmallImageUri(face);
            newPrintingFace.NormalImageURI = NormalImageUri(face);
        }
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

    void ParseCard(const json &object, std::map<std::string, OracleCard>& OUTCards) {
        if (!object.is_object())
            throw std::runtime_error("Each JSONL record must be a card object");

        OracleCard* TargetCard = nullptr;

        //The oracle id will always be present except for reversible_card layout. In that case there will be two found in card_faces
        std::string oracleId = OptionalString(object, "oracle_id");

        std::vector<OracleFace> oracleFaces;
        std::vector<PrintingFace> printingFaces;

        CardFaces(object, oracleFaces, printingFaces);

        //Handle edge case where oracle id is not present in reversible_card layout
        if (oracleId.empty()) {
            if (oracleFaces.empty())
                throw std::runtime_error("No oracle id given and card faces was empty");
            oracleId = oracleFaces[0].OracleId;
        }

        //If we havent parsed this card before, populate the oracle card
        if (auto it = OUTCards.find(oracleId); it == OUTCards.end()) {
            TargetCard = &OUTCards[oracleId];
            TargetCard->OracleId = oracleId;
            TargetCard->Name = RequiredString(object, "name");
            TargetCard->Layout = OptionalString(object, "layout");
            TargetCard->Cmc = OptionalCmc(object);
            TargetCard->Colors = ParseColors(object, "colors");
            TargetCard->ColorIdentity = ParseColors(object, "color_identity");
            TargetCard->Layout = RequiredString(object, "layout");
            TargetCard->Power = OptionalString(object, "power");
            TargetCard->Toughness = OptionalString(object, "toughness");
            TargetCard->Defense = OptionalString(object, "defense");
            TargetCard->Loyalty = OptionalString(object, "loyalty");
            TargetCard->ManaCost = OptionalString(object, "mana_cost");
            TargetCard->Keywords = StringArray(object, "keywords");
            TargetCard->Legalities = Legalities(object);
            TargetCard->OracleText = OptionalString(object, "oracle_text");
            ParseTypes(object, TargetCard->SuperTypes, TargetCard->SubTypes);
        }else {
            //If we already have a card object, use it and update its printings
            TargetCard = &it->second;
        }

        TargetCard->Faces.insert(TargetCard->Faces.end(), oracleFaces.begin(), oracleFaces.end());

        Printing& cardPrinting = TargetCard->Printings.emplace_back();
        cardPrinting.Id = RequiredString(object, "id");
        cardPrinting.Language = RequiredString(object, "lang");
        cardPrinting.CollectorNumber = RequiredString(object, "collector_number");
        cardPrinting.FlavorName = OptionalString(object, "flavor_name");
        cardPrinting.Artist = OptionalString(object, "artist");
        cardPrinting.ArtistIds = StringArray(object, "artist_ids");
        cardPrinting.BorderColor = RequiredString(object, "border_color");
        cardPrinting.Digital = OptionalBool(object, "digital");
        cardPrinting.Finishes = StringArray(object, "finishes");
        cardPrinting.Frame = OptionalString(object, "frame");
        cardPrinting.FrameEffects = StringArray(object, "frame_effects");
        cardPrinting.PromoTypes = StringArray(object, "promo_types");
        cardPrinting.FullArt = OptionalBool(object, "full_art");
        cardPrinting.SmallImageURI = SmallImageUri(object);
        cardPrinting.NormalImageURI = NormalImageUri(object);
        cardPrinting.Oversized = OptionalBool(object, "oversized");
        cardPrinting.Promo = OptionalBool(object, "promo");
        cardPrinting.Rarity = RequiredString(object, "rarity");
        cardPrinting.Reprint = OptionalBool(object, "reprint");
        cardPrinting.SetId = OptionalString(object, "set_id");
        cardPrinting.SetCode = OptionalString(object, "set");
        cardPrinting.SetName = OptionalString(object, "set_name");
        cardPrinting.SetType = OptionalString(object, "set_type");
        cardPrinting.Textless = OptionalBool(object, "textless");
        cardPrinting.Faces.insert(cardPrinting.Faces.end(), printingFaces.begin(), printingFaces.end());
    }

    void ParseLine(const std::filesystem::path &path,
                   std::size_t lineNumber,
                   std::string line,
                   std::map<std::string, OracleCard>& OUTCards) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            return;

        try {
            ParseCard(json::parse(line), OUTCards);
        } catch (const std::exception &error) {
            throw std::runtime_error(path.string() + ": line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }

    void ReadPlainJsonl(const std::filesystem::path &path, std::map<std::string, OracleCard> &cards) {
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

    void ReadGzipJsonl(const std::filesystem::path &path, std::map<std::string, OracleCard> &cards) {
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

std::map<std::string, OracleCard> ScryfallProcessor::Process(std::filesystem::path file, UpdateType updateType) {
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

    std::map<std::string, OracleCard> cards;

    if (IsGzipFile(file))
        ReadGzipJsonl(file, cards);
    else
        ReadPlainJsonl(file, cards);
    return cards;
}

}
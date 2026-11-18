#pragma once

#include "types/card.h"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace magindex::types {
    namespace detail {
        inline std::string FormatColors(Colors colors) {
            constexpr std::array<std::pair<Colors, char>, 6> colorCodes{{
                {Colorless, 'C'}, {White, 'W'}, {Blue, 'U'},
                {Black, 'B'}, {Red, 'R'}, {Green, 'G'}
            }};

            std::string result;
            for (const auto &[mask, code] : colorCodes) {
                if ((colors & mask) == 0) continue;
                if (!result.empty()) result += ", ";
                result += code;
            }
            return result.empty() ? "(none)" : result;
        }

        template<typename Range>
        std::string FormatList(const Range &values) {
            if (values.empty()) return "(none)";
            std::ostringstream out;
            bool first = true;
            for (const auto &value : values) {
                if (!first) out << ", ";
                first = false;
                out << value;
            }
            return out.str();
        }

        inline std::string FormatDate(std::chrono::year_month_day date) {
            if (!date.ok()) return "(unknown)";
            std::ostringstream out;
            out << std::setfill('0') << std::setw(4) << static_cast<int>(date.year()) << '-'
                << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
                << std::setw(2) << static_cast<unsigned>(date.day());
            return out.str();
        }

        inline std::string Display(std::string_view value) {
            return value.empty() ? "(none)" : std::string(value);
        }
    }

    // Formats a card and its nested faces as a readable, multiline debug view.
    inline std::string FormatCard(const Card &card) {
        std::ostringstream out;
        out << "Card: " << detail::Display(card.Name) << '\n'
            << "  Printing ID: " << detail::Display(card.id) << '\n'
            << "  Oracle ID: " << detail::Display(card.OracleId) << '\n'
            << "  Language: " << detail::Display(card.Language) << '\n'
            << "  Layout: " << detail::Display(card.Layout) << '\n'
            << "  Mana cost: " << detail::Display(card.ManaCost) << '\n'
            << "  CMC: " << card.cmc << '\n'
            << "  Colors: " << detail::FormatColors(card.Colors) << '\n'
            << "  Color identity: " << detail::FormatColors(card.ColorIdentity) << '\n'
            << "  Type line: " << detail::Display(card.Typeline) << '\n'
            << "  Power / Toughness: " << detail::Display(card.Power) << " / "
            << detail::Display(card.Toughness) << '\n'
            << "  Loyalty: " << detail::Display(card.Loyalty) << '\n'
            << "  Defense: " << detail::Display(card.Defense) << '\n'
            << "  Oracle text: " << detail::Display(card.OracleText) << '\n'
            << "  Keywords: " << detail::FormatList(card.Keywords) << '\n'
            << "  Supertypes: " << detail::FormatList(card.SuperTypes) << '\n'
            << "  Subtypes: " << detail::FormatList(card.SubTypes) << '\n'
            << "  Legalities:\n";
        if (card.Legalities.empty()) out << "    (none)\n";
        else for (const auto &[format, legality] : card.Legalities) out << "    " << format << ": " << legality << '\n';

        out << "  Faces (" << card.CardFaces.size() << "):\n";
        if (card.CardFaces.empty()) out << "    (none)\n";
        for (const CardFace &face : card.CardFaces) {
            out << "    [" << static_cast<unsigned>(face.Ordinal) << "] " << detail::Display(face.Name) << '\n'
                << "      Oracle ID: " << detail::Display(face.OracleId) << '\n'
                << "      Mana cost: " << detail::Display(face.ManaCost) << " | CMC: " << face.Cmc << '\n'
                << "      Colors: " << detail::FormatColors(face.Colors) << '\n'
                << "      Type line: " << detail::Display(face.TypeLine) << '\n'
                << "      Power / Toughness: " << detail::Display(face.Power) << " / "
                << detail::Display(face.Toughness) << '\n'
                << "      Loyalty: " << detail::Display(face.Loyalty) << '\n'
                << "      Defense: " << detail::Display(face.Defense) << '\n'
                << "      Supertypes: " << detail::FormatList(face.SuperTypes) << '\n'
                << "      Subtypes: " << detail::FormatList(face.SubTypes) << '\n'
                << "      Artist: " << detail::Display(face.Artist) << '\n'
                << "      Artist ID: " << detail::Display(face.ArtistId) << '\n'
                << "      Small image: " << detail::Display(face.SmallImageURI) << '\n'
                << "      Normal image: " << detail::Display(face.NormalImageURI) << '\n';
        }

        out << "  Printing:\n"
            << "    Set: " << detail::Display(card.SetName) << " (" << detail::Display(card.SetCode) << ")\n"
            << "    Set ID: " << detail::Display(card.SetId) << '\n'
            << "    Set type: " << detail::Display(card.SetType) << '\n'
            << "    Released: " << detail::FormatDate(card.ReleasedAt) << '\n'
            << "    Collector number: " << detail::Display(card.CollectorNumber) << '\n'
            << "    Rarity: " << detail::Display(card.Rarity) << '\n'
            << "    Artist: " << detail::Display(card.Artist) << '\n'
            << "    Artist IDs: " << detail::FormatList(card.ArtistIds) << '\n'
            << "    Flavor name: " << detail::Display(card.FlavorName) << '\n'
            << "    Frame: " << detail::Display(card.Frame) << '\n'
            << "    Frame effects: " << detail::FormatList(card.FrameEffects) << '\n'
            << "    Promo types: " << detail::FormatList(card.PromoTypes) << '\n'
            << "    Finishes: " << detail::FormatList(card.Finishes) << '\n'
            << "    Border color: " << detail::Display(card.BorderColor) << '\n'
            << "    Digital: " << std::boolalpha << card.bDigital << '\n'
            << "    Full art: " << card.bFullArt << '\n'
            << "    Oversized: " << card.bOversized << '\n'
            << "    Promo: " << card.bPromo << '\n'
            << "    Reprint: " << card.bReprint << '\n'
            << "    Textless: " << card.bTextless << '\n'
            << "    Small image: " << detail::Display(card.SmallImageURI) << '\n'
            << "    Normal image: " << detail::Display(card.NormalImageURI);
        return out.str();
    }
}

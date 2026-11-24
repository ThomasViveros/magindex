#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "types/color.h"

namespace magindex::types {
    struct OracleFace {
        //OracleId may be null
        std::string OracleId;
        std::uint8_t Ordinal = 0;
        std::string Name;
        float Cmc = 0.0F;
        Colors Colors = NA;
        std::set<std::string> SuperTypes;
        std::set<std::string> SubTypes;
        std::string ManaCost;
        std::string Defense;
        std::string Power;
        std::string Toughness;
        std::string Loyalty;
        std::string TypeLine;
    };

    struct PrintingFace {
        std::uint8_t Ordinal = 0;
        std::string Name;
        std::string Artist;
        std::string ArtistId;
        std::string SmallImageURI;
        std::string NormalImageURI;
    };

    struct Printing {
        std::string Id;
        std::string Language;
        std::string CollectorNumber;
        std::string FlavorName;
        std::string Artist;
        std::vector<std::string> ArtistIds;
        std::string BorderColor;
        bool Digital = false;
        std::vector<std::string> Finishes;
        std::string Frame;
        std::vector<std::string> FrameEffects;
        std::vector<std::string> PromoTypes;
        bool FullArt = false;
        std::string SmallImageURI;
        std::string NormalImageURI;
        bool Oversized = false;
        bool Promo = false;
        std::string Rarity;
        bool Reprint = false;
        std::string SetId;
        std::string SetCode;
        std::string SetName;
        std::string SetType;
        bool Textless = false;
        std::vector<PrintingFace> Faces;
    };

    // Public query model: one Oracle card with its applicable printings.
    struct OracleCard {
        std::string OracleId;
        std::string Name;
        std::string Layout;
        float Cmc = 0.0F;
        Colors ColorIdentity = NA;
        Colors Colors = NA;
        std::string Power;
        std::string Toughness;
        std::string Defense;
        std::string Loyalty;
        std::string ManaCost;
        std::string TypeLine;
        std::vector<std::string> Keywords;
        std::map<std::string, std::string> Legalities;
        std::string OracleText;
        std::set<std::string> SuperTypes;
        std::set<std::string> SubTypes;
        std::vector<OracleFace> Faces;
        std::vector<Printing> Printings;
    };

    struct OracleSet {
        OracleSet() = default;
        OracleSet(const std::string& InId, const std::string& InCode, const std::string& InName, const std::chrono::year_month_day& InReleasedAt, const std::string& InType) :
        Id(InId), Code(InCode), Name(InName), ReleasedAt(InReleasedAt), Type(InType) {}
        std::string Id;
        std::string Code;
        std::string Name;
        std::chrono::year_month_day ReleasedAt;
        std::string Type;
    };

    struct PageRequest {
        // Zero-based offset into the ordered set of matching Oracle cards.
        std::uint64_t Offset = 0;
        // Number of Oracle cards in the page. Storage currently caps this at 1000.
        std::uint32_t Limit = 50;
    };

    struct QueryPage {
        std::vector<OracleCard> Cards;
        // Total distinct Oracle cards matching the query, before pagination.
        std::uint64_t TotalCards = 0;
        PageRequest Page;

        [[nodiscard]] bool HasMore() const noexcept {
            return Page.Offset < TotalCards && Cards.size() < TotalCards - Page.Offset;
        }
    };
}

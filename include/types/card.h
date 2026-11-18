#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "types/color.h"

namespace magindex::types {
    struct CardFace {
        std::string Name;
        //Used to determine what face should be dominant. Lower number is more dominant
        std::uint8_t Ordinal = 0;
        //May be null
        std::string OracleId;
        //Optional
        std::string Artist;
        //Optional
        std::string ArtistId;
        //Optional
        float Cmc = 0;
        //Optional
        Colors Colors = NA;
        //May be empty
        std::set<std::string> SuperTypes;
        //May be empty
        std::set<std::string> SubTypes;
        //May be empty
        std::string ManaCost;
        //Optional
        std::string Defense;
        //Optional
        std::string Power;
        //Optional
        std::string Toughness;
        //Optional
        std::string Loyalty;
        //Optional
        std::string TypeLine;
        //Optional
        std::string SmallImageURI;
        //Optional
        std::string NormalImageURI;
    };

    struct Card {
        //===========================Core card fields==========================
        std::string id;
        //OracleId may be invalid,
        std::string OracleId;
        std::string Language;
        std::string Layout;


        //===========================Gameplay Fields===========================
        //Optional
        std::vector<CardFace> CardFaces;
        float cmc = 0.0F;
        Colors ColorIdentity = NA;
        //Optional
        Colors Colors = NA;
        //Optional
        std::string Power;
        //Optional
        std::string Toughness;
        //Optional
        std::string Defense;
        //Optional
        std::string Loyalty;
        //May be empty
        std::string ManaCost;
        std::string Typeline;
        std::vector<std::string> Keywords;
        std::map<std::string, std::string> Legalities;
        std::string Name;
        //Optional
        std::string OracleText;
        //May be empty
        std::set<std::string> SuperTypes;
        //May be empty
        std::set<std::string> SubTypes;


        //============================Print Fields================================
        //Optional
        std::string Artist;
        //Optional
        std::vector<std::string> ArtistIds;
        std::string BorderColor;
        std::string CollectorNumber;
        bool bDigital = false;
        std::vector<std::string> Finishes;
        //Optional
        std::string FlavorName;
        std::string Frame;
        //optional
        std::vector<std::string> FrameEffects;
        //Optional
        std::set<std::string> PromoTypes;
        bool bFullArt = false;
        //May be empty
        std::string SmallImageURI;
        //May be empty
        std::string NormalImageURI;
        bool bOversized = false;
        bool bPromo = false;
        std::string Rarity;
        std::chrono::year_month_day ReleasedAt;
        bool bReprint = false;
        std::string SetId;
        std::string SetCode;
        std::string SetName;
        std::string SetType;
        bool bTextless = false;
    };
}

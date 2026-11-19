#pragma once
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "color.h"
#include "rarity.h"

namespace magindex::types {

enum EqualityOperators : uint8_t {
    GreaterThan,
    GreaterThanOrEqualTo,
    LessThan,
    LessThanOrEqualTo,
    EqualTo,
};

enum  LogicalOperators : uint8_t {
    And,
    Or
};

enum ColorOperators : uint8_t {
    Exactly,
    AtMost,
    Including,
};

enum LegalityOperators : uint8_t {
    Legal,
    Banned,
    NotLegal,
};

using NumericalQuery = std::tuple<EqualityOperators, int32_t, LogicalOperators>;

class CardQuery {
public:

    CardQuery *NameParam(std::string InName) {
        _name = std::move(InName);
        return this;
    }


    CardQuery *SetsParam(std::vector<std::string> InSets) {
        _sets = std::move(InSets);
        return this;
    }

    //TODO: for the numerical functions maybe have a vector version?

    //Cumulative
    CardQuery *PowerParam(EqualityOperators PowerEquality, int32_t InPower, LogicalOperators PowerLogic = LogicalOperators::And) {
        _power.emplace_back(PowerEquality, InPower, PowerLogic);
        return this;
    }
    //Cumulative
    CardQuery *ToughnessParam(EqualityOperators ToughnessEquality, int32_t InToughness, LogicalOperators ToughnessLogic = LogicalOperators::And) {
        _toughness.emplace_back(ToughnessEquality, InToughness, ToughnessLogic);
        return this;
    }
    //Cumulative
    CardQuery *LoyaltyParam(EqualityOperators LoyaltyEquality, int32_t InLoyalty, LogicalOperators LoyaltyLogic = LogicalOperators::And) {
        _loyalty.emplace_back(LoyaltyEquality, InLoyalty, LoyaltyLogic);
        return this;
    }
    //Cumulative
    CardQuery *DefenseParam(EqualityOperators DefenseEquality, int32_t InDefense, LogicalOperators DefenseLogic = LogicalOperators::And) {
        _defense.emplace_back(DefenseEquality, InDefense, DefenseLogic);
        return this;
    }

    //Not cummulative
    CardQuery *ColorsParam(ColorOperators ColorOperator, Colors InColors, bool bIsColorIdentity = false) {
        if (InColors == Color::NA) {
            throw std::invalid_argument("Invalid color operator");
        }
        _colors = {ColorOperator, InColors};
        _hasColors = true;
        _isColorIdentity = bIsColorIdentity;
        return this;
    }

    //Will move InLegalities
    CardQuery *LegalitiesParam( std::map<std::string, LegalityOperators> InLegalities) {
        _legalities = std::move(InLegalities);
        return this;
    }

    //Not cumulative
    CardQuery *SupertypesParam(std::vector<std::string> InSupertypes) {
        _supertypes = std::move(InSupertypes);
        return this;
    }

    //Not cumulative
    CardQuery *SubtypesParam(std::vector<std::string> InSubtypes) {
        _subtypes = std::move(InSubtypes);
        return this;
    }

    //Not cumulative
    CardQuery *ManaCostParam(std::vector<std::shared_ptr<ManaSymbol>> InManaSymbols) {
        _manaSymbols = std::move(InManaSymbols);
        return this;
    }

    //Cumulative
    CardQuery* RarityParam(Rarity InRarity) {
        _rarity.push_back(InRarity);
        return this;
    }
    //Cumulative
    CardQuery* CMCParam(EqualityOperators EqualityOperator, int32_t InCMC, LogicalOperators CMCLogic = LogicalOperators::And) {
        _cmc.emplace_back(EqualityOperator, InCMC, CMCLogic);
        return this;
    }

    [[nodiscard]] const std::string &Name() const noexcept { return _name; }
    [[nodiscard]] const std::vector<std::string> &Sets() const noexcept { return _sets; }
    [[nodiscard]] const std::vector<NumericalQuery> &Power() const noexcept { return _power; }
    [[nodiscard]] const std::vector<NumericalQuery> &Toughness() const noexcept { return _toughness; }
    [[nodiscard]] const std::vector<NumericalQuery> &Loyalty() const noexcept { return _loyalty; }
    [[nodiscard]] const std::vector<NumericalQuery> &Defense() const noexcept { return _defense; }
    [[nodiscard]] bool HasColors() const noexcept { return _hasColors; }
    [[nodiscard]] bool IsColorIdentity() const noexcept { return _isColorIdentity; }
    [[nodiscard]] const auto &ColorsFilter() const noexcept { return _colors; }
    [[nodiscard]] const std::map<std::string, LegalityOperators> &Legalities() const noexcept { return _legalities; }
    [[nodiscard]] const std::vector<std::string> &Supertypes() const noexcept { return _supertypes; }
    [[nodiscard]] const std::vector<std::string> &Subtypes() const noexcept { return _subtypes; }
    [[nodiscard]] const std::vector<std::shared_ptr<ManaSymbol>> &ManaSymbols() const noexcept { return _manaSymbols; }
    [[nodiscard]] const std::vector<Rarity> &Rarities() const noexcept { return _rarity; }
    [[nodiscard]] const std::vector<NumericalQuery> &CMC() const noexcept { return _cmc; }

private:

    std::string _name;
    std::vector<std::string> _sets;
    std::vector<NumericalQuery> _power;
    std::vector<NumericalQuery> _toughness;
    std::vector<NumericalQuery> _loyalty;
    std::vector<NumericalQuery> _defense;
    std::tuple<ColorOperators, Colors> _colors;
    bool _hasColors = false;
    //If the colors should represent the identity.
    bool _isColorIdentity = false;
    std::map<std::string, LegalityOperators> _legalities;
    std::vector<std::string> _supertypes;
    std::vector<std::string> _subtypes;
    std::vector<std::shared_ptr<ManaSymbol>> _manaSymbols;
    std::vector<Rarity> _rarity;
    std::vector<NumericalQuery> _cmc;
};
}
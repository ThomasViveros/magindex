#pragma once
#include <cstdint>
#include <format>
#include <stdexcept>
#include <map>
#include <string>


namespace magindex::types {


//Colors are added with Color bitmask
using Colors = std::uint8_t;

//Color bitmask
enum Color : uint8_t {
    NA = 0,
    Colorless = 1 << 0,
    White = 1 << 1,
    Blue = 1 << 2,
    Black = 1 << 3,
    Red = 1 << 4,
    Green = 1 << 5
};

constexpr Color CharToColor(char c) {
    switch (c) {
        case 'C' : return Colorless;
        case 'W' : return White;
        case 'U' : return Blue;
        case 'B' : return Black;
        case 'R' : return Red;
        case 'G' : return Green;
        default: break;
    }
    throw std::invalid_argument("No mana color code exists for this color value");
};

constexpr char ManaColorCode(Color color) {
    switch (color) {
        case Color::Colorless: return 'C';
        case Color::White: return 'W';
        case Color::Blue: return 'U';
        case Color::Black: return 'B';
        case Color::Red: return 'R';
        case Color::Green: return 'G';
        case Color::NA: break;
    }
    throw std::invalid_argument("No mana color code exists for this color value");
}

class ManaSymbol {
public:
    virtual ~ManaSymbol() = default;

    virtual std::string GetManaSymbolString() = 0;
};


class MonoManaSymbol : ManaSymbol {
public:
    MonoManaSymbol() = delete;

    explicit MonoManaSymbol(Color InColor) : _color(InColor) {
    };

    std::string GetManaSymbolString() override {
        return std::format("{{{}}}", ManaColorCode(_color));
    }

private:
    Color _color;
};

class DualManaSymbol : ManaSymbol {
public:
    DualManaSymbol() = delete;

    explicit DualManaSymbol(Color InColorOne, Color InColorTwo) {
        if (InColorOne == InColorTwo) {
            throw std::logic_error("DualManaSymbol cannot be used with the same color");
        }
        if (InColorOne < InColorTwo) {
            PrimaryColor = InColorOne;
            SecondaryColor = InColorTwo;
        } else {
            PrimaryColor = InColorTwo;
            SecondaryColor = InColorOne;
        }
    }

    std::string GetManaSymbolString() override {
        return std::format("{{{}/{}}}", ManaColorCode(PrimaryColor), ManaColorCode(SecondaryColor));
    }

private:
    Color PrimaryColor, SecondaryColor;
};

class DiscountManaSymbol : ManaSymbol {
public:
    DiscountManaSymbol() = delete;

    explicit DiscountManaSymbol(Color InColor) {
        if (InColor == Color::Colorless) {
            throw std::logic_error("DiscountManaSymbol cannot be colorless");
        }
        Color = InColor;
    }

    std::string GetManaSymbolString() override {
        return std::format("{{2/{}}}", ManaColorCode(Color));
    }

private:
    Color Color;
};

class MonoPainManaSymbol : ManaSymbol {
public:
    MonoPainManaSymbol() = delete;

    explicit MonoPainManaSymbol(Color InColor) {
        if (InColor == Color::Colorless) {
            throw std::logic_error("MonoPainManaSymbol cannot be colorless");
        }
        Color = InColor;
    }

    std::string GetManaSymbolString() override {
        return std::format("{{{}/P}}", ManaColorCode(Color));
    }

private:
    Color Color;
};

class DualPainManaSymbol : ManaSymbol {
public:
    DualPainManaSymbol() = delete;

    explicit DualPainManaSymbol(Color InColorOne, Color InColorTwo) {
        if (InColorOne == Color::Colorless || InColorTwo == Color::Colorless) {
            throw std::logic_error("DualPainManaSymbol cannot have colorless");
        }
        if (InColorOne == InColorTwo) {
            throw std::logic_error("DualPainManaSymbol cannot have two of the same color");
        }

        if (InColorOne < InColorTwo) {
            PrimaryColor = InColorOne;
            SecondaryColor = InColorTwo;
        } else {
            PrimaryColor = InColorTwo;
            SecondaryColor = InColorOne;
        }
    }

    std::string GetManaSymbolString() override {
        return std::format("{{{}/{}/P}}", ManaColorCode(PrimaryColor), ManaColorCode(SecondaryColor));
    }

private:
    Color PrimaryColor, SecondaryColor;
};

class SnowManaSymbol : ManaSymbol {
public:
    std::string GetManaSymbolString() override {
        return std::string{"{S}"};
    }
};

}
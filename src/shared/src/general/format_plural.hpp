#pragma once
#include <concepts>
#include <string>
template <std::integral I>
std::string format_plural(I amount, std::string_view unit)
{
    bool isPlural { amount != 1 };
    return std::to_string(amount) + " " + std::string(unit) + (isPlural ? "s" : "");
}

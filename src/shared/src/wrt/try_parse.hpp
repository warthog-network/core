#pragma once
#include <charconv>
#include <concepts>
#include <optional>
#include <string_view>

template <typename T>
requires(std::integral<T> || std::floating_point<T>)
[[nodiscard]] std::optional<T> try_parse(std::string_view s)
{
    T t;
    auto last { s.data() + s.size() };
    auto result = std::from_chars(s.data(), last, t);
    if (result.ec != std::errc {} || result.ptr != last)
        return {};
    return t;
}

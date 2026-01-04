#pragma once
#include <cstddef>
#include <string>

template <size_t N>
struct StaticString {
    constexpr StaticString(const char (&str)[N])
    {
        for (size_t i { 0 }; i < N; ++i)
            value[i] = str[i];
    }
    std::string to_string() const
    {
        if (N == 0)
            return {};
        return std::string { value, N - 1 };
    }
    operator std::string_view() const
    {
        if (N == 0)
            return {};
        return { value, N - 1 };
    }

    char value[N];
};

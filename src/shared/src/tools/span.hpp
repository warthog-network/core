#pragma once
#include <span>
#include <vector>

template <typename T, size_t N>
auto to_array(std::span<T, N> s)
{
    std::array<T, N> arr;
    std::copy(std::move(s).begin(), std::move(s).end(), arr);
    return arr;
}

template <typename T, size_t N>
auto to_array(std::span<const T, N> s)
{
    std::array<T, N> arr;
    std::copy(s.begin(), s.end(), arr);
    return arr;
}

template <typename T>
auto to_vector(std::span<T> s)
{
    return std::vector<T>(std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
}

template <typename T>
auto to_vector(std::span<const T> s)
{
    return std::vector<T>(s.begin(), s.end());
}

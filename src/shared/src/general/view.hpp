#pragma once
#include "general/fwd.hpp"
#include "general/serializer_fwd.hxx"
#include "view_fwd.hpp"
#include <array>
#include <cassert>
#include <span>
template <size_t N>
struct View {
    static const size_t length;
    static constexpr size_t size() { return N; }
    static constexpr size_t byte_size() { return N; }
    void serialize(RawSerializer auto&& s) const
    {
        FWD(s) << span();
    }
    bool is_null() const { return pos == nullptr; }
    const uint8_t* data() const { return pos; }
    const uint8_t* begin() const { return data(); }
    const uint8_t* end() const { return begin() + N; }
    uint8_t operator[](size_t i) const { return *(pos + i); }
    std::span<const uint8_t> span() const { return { data(), size() }; }
    operator std::span<const uint8_t>() const { return span(); }
    operator std::array<uint8_t, N>()
    {
        std::array<uint8_t, N> res;
        std::copy(pos, pos + N, res.begin());
        return res;
    }
    auto operator<=>(const View& v) const
    {
        assert(!is_null() && !v.is_null());
        return std::lexicographical_compare_three_way(begin(),end(), v.begin(),v.end());
    }
    bool operator==(const View& v) const
    {
        return operator<=>(v) == 0;
    }
    View(const std::array<uint8_t, N>& a)
        : View(a.data())
    {
    }
    View(const uint8_t* pos)
        : pos(pos)
    {
    }

protected:
    const uint8_t* pos;
};

template <size_t N>
inline constexpr size_t View<N>::length { N };

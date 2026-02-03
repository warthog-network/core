#pragma once
#include "errors.hpp"
#include "general/byte_order.hpp"
#include "hex_digit.hpp"
#include <span>
#include <array>
#include <string>
#include <vector>


inline void serialize_hex(std::span<const uint8_t> in, std::output_iterator<char> auto out)
{
    constexpr const char* h = "0123456789abcdef";
    for (auto u: in) {
        *(out++) = h[u >> 4];
        *(out++) = h[u & 15];
    }
}

void serialize_hex(uint32_t number, std::output_iterator<char> auto out)
{
    uint32_t tmp = hton32(number);
    serialize_hex({(const uint8_t*)&tmp, 4}, out);
}

std::string serialize_hex(std::span<const uint8_t> in);
std::string serialize_hex(uint32_t v);

struct HexRef {
    std::string_view hexStr;
    constexpr size_t byte_size() const { return hexStr.size() / 2; }
    constexpr HexRef(std::string_view hexStr)
        : hexStr(hexStr)
    {
        if (hexStr.size() % 2 != 0)
            throw Error(EINV_HEX);
    }
    constexpr bool insert_into(std::output_iterator<uint8_t> auto iter) const
    {
        bool valid = true;
        for (size_t i = 0; i < hexStr.size() / 2; ++i) {
            *(iter++) = (hex_digit(hexStr[2 * i], valid) << 4)
                + (hex_digit(hexStr[2 * i + 1], valid));
        }
        if (!valid) {
            throw Error(EINV_HEX);
        }
        return valid;
    }
    constexpr bool parse_to(std::span<uint8_t> out) const
    {
        if (hexStr.size() != out.size() * 2){
            throw Error(EINV_HEX);
            return false;
        }
        return insert_into(out.begin());
    }
    constexpr void place_into_throw(std::span<uint8_t> out) const
    {
        if (!parse_to(out))
            throw Error(EINV_HEX);
    }
    constexpr void place_into_throw(std::output_iterator<uint8_t> auto iter) const
    {
        if (!insert_into(iter))
            throw Error(EINV_HEX);
    }
    template <size_t N>
    constexpr operator std::array<uint8_t, N>() const
    {
        std::array<uint8_t, N> out;
        place_into_throw(out);
        return out;
    }
    operator std::vector<uint8_t>() const
    {
        std::vector<uint8_t> v(byte_size());
        parse_to(v);
        return v;
    }
};

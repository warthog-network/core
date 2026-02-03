#include "header.hpp"
#include "general/hex.hpp"
namespace {
}

Header::Header(std::string_view v)
    : Header(HexRef(v))
{
}
Header::Header(std::span<const uint8_t, 80>& s)
    : Header([&]() -> std::array<uint8_t, 80> {
        std::array<uint8_t, 80> a;
        std::copy(s.begin(), s.end(), a.begin());
        return a;
    }())
{
}
void Header::set_timestamp(std::array<uint8_t, 4> arr)
{
    std::copy(arr.begin(), arr.end(), begin() + HeaderView::offset_timestamp);
}
void Header::set_nonce(std::array<uint8_t, 4> arr)
{
    std::copy(arr.begin(), arr.end(), begin() + HeaderView::offset_nonce);
}

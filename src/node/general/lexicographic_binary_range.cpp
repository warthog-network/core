#include "lexicographic_binary_range.hpp"
#include "general/hex_digit.hpp"
wrt::optional<LexicographicByteRange> LexicographicByteRange::from_hex(std::string_view hexPrefix)
{
    size_t l { hexPrefix.length() };
    LexicographicByteRange res;
    auto& begin { res.begin };
    auto& end { res.end };
    begin.reserve((l + 1) / 2);
    size_t lastNonFF { 0 }; // position + 1 of last scanned byte != 0xFF
    bool fresh { true };
    bool valid { true };
    for (auto c : hexPrefix) {
        if (fresh) {
            begin.push_back(hex_digit(c, valid) << 4);
        } else {
            begin.back() += hex_digit(c, valid);
            if (begin.back() != 255)
                lastNonFF = begin.size();
        }
        fresh = !fresh;
    }
    if (!valid)
        return {};
    if (!fresh && (begin.back() >> 4) != 15) {
        end = begin;
        end->back() += 1 << 4;
    }
    if (!end && lastNonFF != 0) {
        end = std::vector<uint8_t>(begin.begin(), begin.begin() + lastNonFF);
        end->back() += 1;
    }
    return res;
}

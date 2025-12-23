#pragma once
#include "general/with_uint64.hpp"
#include "general/errors_forward.hpp"
class BlockVersion : public IsUint32 {
public:
    explicit constexpr BlockVersion(uint32_t v)
        : IsUint32(v)
    {
    }
    static BlockVersion hardcoded_for_params(NonzeroHeight h);

    static const BlockVersion v3;
    static const BlockVersion v4;

    using IsUint32::operator<=>;
    auto operator<=>(uint32_t v)
    {
        return value() <=> (v);
    }
    constexpr bool operator==(const BlockVersion& v) const = default;
    auto operator==(uint32_t v)
    {
        return value() == v;
    }
};
inline constexpr const BlockVersion BlockVersion::v3 { 3 };
inline constexpr const BlockVersion BlockVersion::v4 { 4 };

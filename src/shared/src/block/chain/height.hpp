#pragma once
#include "general/errors.hpp"
#include "general/funds.hpp"

class PinHeight;
class NonzeroHeight;
class Height : public IsUint32 {
    friend struct Batchslot;

protected:
    constexpr Height()
        : IsUint32(uint32_t(0))
    {
    }

public:
    using IsUint32::IsUint32;
    static Height undef() { return Height { 0 }; }
    Height retarget_floor()
    {
        return Height { ::retarget_floor(val) };
    }
    Height(const Height&) = default;

    Height& operator=(const Height&) = default;

    NonzeroHeight nonzero_assert() const;
    NonzeroHeight nonzero_throw(int Error) const;
    NonzeroHeight one_if_zero() const;
    Height& operator--()
    {
        assert(val > 0);
        val -= 1;
        return *this;
    }
    Height& operator++()
    {
        val += 1;
        return *this;
    }
    Height& operator-=(uint32_t v)
    {
        assert(val >= v);
        val -= v;
        return *this;
    }
    uint32_t friend operator-(Height h1, Height h2)
    {
        return h1.val - h2.val;
    }
    Height friend operator-(Height h1, uint32_t r)
    {
        return Height(h1.val - r);
    }
    Height friend operator+(Height h1, uint32_t r)
    {
        return Height(h1.val + r);
    }

    size_t complete_batches() const
    {
        return val / HEADERBATCHSIZE;
    }
    size_t incomplete_batch_size() const
    {
        return val % HEADERBATCHSIZE;
    }
    Funds reward() const
    {
        uint64_t halvings = (val - 1) / HALVINTINTERVAL;
        if (halvings >= 64)
            return Funds::zero();
        return Funds::from_value(GENESISBLOCKREWARD >> halvings).value();
    }

    Height pin_begin()
    {
        uint32_t shifted = val >> 5;
        if (shifted < 255u)
            return Height(0);
        return Height((shifted - 255u) << 5);
    }
    std::optional<PinHeight> pin_height() const;
    bool is_pin_height() const;

    friend bool operator==(const Height& h1, uint32_t h)
    {
        return h1.val == h;
    }
};


class NonzeroHeight : public IsUint32 {
    friend struct Batchslot;

public:
    NonzeroHeight(Reader& r);
    explicit NonzeroHeight(Height h)
        : NonzeroHeight(h.value())
    {
    }
    NonzeroHeight(bool) = delete;
    constexpr NonzeroHeight(uint32_t v)
        : IsUint32(v)
    {
        assert(v != 0);
    }

    operator Height() const
    {
        auto v { value() };
        return Height(v);
    }
    NonzeroHeight retarget_floor() const
    {
        return NonzeroHeight { ::retarget_floor(val) };
    }
    NonzeroHeight(const NonzeroHeight&) = default;

    NonzeroHeight& operator=(const NonzeroHeight&) = default;

    bool is_retarget_height() const
    {
        return *this == retarget_floor();
    }
    NonzeroHeight& operator--()
    {
        assert(val > 1);
        val -= 1;
        return *this;
    }
    NonzeroHeight& operator++()
    {
        val += 1;
        return *this;
    }
    NonzeroHeight& operator-=(uint32_t v)
    {
        assert(val >= v + 1);
        val -= v;
        return *this;
    }
    uint32_t friend operator-(NonzeroHeight h1, NonzeroHeight h2)
    {
        return h1.val - h2.val;
    }
    uint32_t friend operator-(NonzeroHeight h1, Height h2)
    {
        return static_cast<Height>(h1) - h2;
    }
    Height friend operator-(NonzeroHeight h1, uint32_t r)
    {
        return Height(h1.val - r);
    }
    NonzeroHeight friend operator+(NonzeroHeight h1, uint32_t r)
    {
        return NonzeroHeight(h1.val + r);
    }

    size_t complete_batches() const
    {
        return val / HEADERBATCHSIZE;
    }
    size_t incomplete_batch_size() const
    {
        return val % HEADERBATCHSIZE;
    }
    Funds reward() const
    {
        uint64_t halvings = (val - 1) / HALVINTINTERVAL;
        if (halvings >= 64)
            return Funds::zero();
        return Funds::from_value(GENESISBLOCKREWARD >> halvings).value();
    }

    // NonzeroHeight pin_bgin()
    // {
    //     uint32_t shifted = val >> 5;
    //     if (shifted < 255u)
    //         return NonzeroHeight(0);
    //     return NonzeroHeight((shifted - 255u) << 5);
    // }

    friend bool operator==(const NonzeroHeight& h1, uint32_t h)
    {
        return h1.val == h;
    }
};

class PrevHeight : public Height {
public:
    explicit PrevHeight(NonzeroHeight h)
        : Height(h - 1)
    {}
};

inline NonzeroHeight Height::nonzero_assert() const
{
    return { NonzeroHeight(value()) };
}

inline NonzeroHeight Height::nonzero_throw(int error) const
{
    if (val == 0) {
        throw Error(error);
    }
    return nonzero_assert();
}

inline NonzeroHeight Height::one_if_zero() const
{
    if (val == 0) {
        return NonzeroHeight(1u);
    }
    return NonzeroHeight(value());
}

class PinHeight : public Height {
public:
    explicit PinHeight(const Height h);
};
struct AccountHeight : public Height {
    using Height::Height;
};

struct TransactionHeight : public Height {
    TransactionHeight(PinHeight ph, AccountHeight ah)
        : Height(std::max(Height(ph), Height(ah)))
    {
    }
};

struct PinFloor : public Height {
    using Height::Height;
    explicit PinFloor(PrevHeight h)
        : Height((h.value() >> 5) << 5) //& 0xFFFFFFE0u;
    {
    }
};

inline bool Height::is_pin_height() const
{
    return (val & 0x0000001F) == 0;
}
inline std::optional<PinHeight> Height::pin_height() const
{
    if (!is_pin_height())
        return {};
    return PinHeight(*this);
}

namespace std {
std::string inline to_string(Height h) { return std::to_string(h.value()); }

}

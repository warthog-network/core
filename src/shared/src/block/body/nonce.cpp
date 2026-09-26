#include "nonce.hpp"
#include "block/chain/height.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/reader.hpp"
#include "general/writer.hpp"
#include "wrt/try_parse.hpp"
#include <algorithm>
#include <climits>
#include <random>
NonceId NonceId::random()
{
    std::array<uint8_t, 4> rand;
    std::independent_bits_engine<std::random_device, CHAR_BIT, uint8_t> e;
    std::generate(std::begin(rand), std::end(rand), std::ref(e));
    uint32_t val;
    memcpy(&val, rand.data(), rand.size());
    return NonceId(val);
};

std::optional<NonceId> NonceId::try_parse(std::string_view s)
{
    return ::try_parse<uint32_t>(s)
        .transform([](uint32_t i) { return NonceId { i }; });
}

NonceReserved::NonceReserved(Reader& r)
    : NonceReserved(r.view<3>())
{
}

PinNonce::PinNonce(ReaderCheck<bytesize> r)
    : id(r.r)
    , relativePin(r.r)
    , reserved(r.r)
{
    r.assert_read_bytes();
}

PinNonce::PinNonce(Reader& r)
    : PinNonce(ReaderCheck<bytesize>(r)) { };

PinHeight PinNonce::pin_height_from_floored(PinFloor pf) const
{
    Height h(pf - std::min(pin_offset(), pf.value()));
    assert(h.is_pin_height());
    return PinHeight(h);
}

std::optional<PinNonce> PinNonce::make_pin_nonce(NonceId nid, NonzeroHeight height, PinHeight pinHeight)
{
    auto ph { height.pin_floor() };
    if (ph < pinHeight)
        return {};
    uint64_t index = ((ph - pinHeight) >> 5);
    if (index > 255u)
        return {};
    return PinNonce(nid, (uint8_t)index);
}

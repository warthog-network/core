#include "hash.hpp"
#include "general/hex.hpp"
#include "general/is_testnet.hpp"
#include "general/params.hpp"
#include "hasher_sha256.hpp"

std::string Hash::hex_string() const
{
    return serialize_hex(*this);
}

std::optional<Hash> Hash::try_parse(std::string_view hex)
{
    auto h { uninitialized() };
    if (HexRef(hex).parse_to(h))
        return h;
    return {};
}

Hash Hash::parse_throw(std::string_view s)
{
    auto o { try_parse(s) };
    if (o)
        return *o;
    throw Error(EPARSEHASH);
}

BlockHash BlockHash::genesis()
{
    auto makeBlockHash([](const char* seed) {
        return BlockHash(hashSHA256(reinterpret_cast<const uint8_t*>(seed),
            strlen(seed)));
    });
    return makeBlockHash(is_testnet() ? GENESISSEED_TESTNET : GENESISSEED);
};

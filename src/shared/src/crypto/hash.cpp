#include "hash.hpp"
#include "general/hex.hpp"
#include "general/params.hpp"
#include "hasher_sha256.hpp"

std::string Hash::hex_string() const
{
    return serialize_hex(*this);
}

wrt::optional<Hash> Hash::try_parse(std::string_view hex)
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
    return BlockHash(hashSHA256(reinterpret_cast<const uint8_t*>(GENESISSEED),
        strlen(GENESISSEED)));
};

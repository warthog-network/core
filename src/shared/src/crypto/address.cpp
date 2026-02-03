#include "address.hpp"
#include "general/hex.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/hex.hpp"

std::array<uint8_t, 24> AddressView::serialize() const
{
    std::array<uint8_t, 24> ret;
    memcpy(ret.data(), data(), size());
    Hash h { hashSHA256(data(), size()) };
    memcpy(ret.data() + 20, h.data(), 4);
    return ret;
};

std::string AddressView::to_string() const
{
    return serialize_hex(serialize());
};

Address::Address(const std::string_view address)
{
    using namespace std::literals;
    std::array<uint8_t, 24> bytes;
    if (!HexRef(address).parse_to(bytes))
        throw Error(EBADADDRESS);

    auto hash = hashSHA256(bytes.data(), 20);
    if (memcmp(bytes.data() + 20, hash.data(), 4) != 0) {
        throw Error(EBADADDRESS);
    }
    memcpy(data(), bytes.data(), 20);
}

Address& Address::operator=(const AddressView rhs)
{
    memcpy(data(), rhs.data(), 20);
    return *this;
};

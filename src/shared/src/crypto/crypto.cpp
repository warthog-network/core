#include "crypto.hpp"
#include "general/errors.hpp"
#include "general/hex.hpp"
#include "hasher_sha256.hpp"
#include "general/writer.hpp"
#include "ripemd160.hpp"

namespace {
secp256k1_context* secp256k1_ctx = nullptr;
}

// #include <vector>

void ECC_Start()
{
    assert(secp256k1_ctx == nullptr);
    secp256k1_ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);
    assert(secp256k1_ctx != nullptr);
}

void ECC_Stop()
{
    if (secp256k1_ctx)
        secp256k1_context_destroy(secp256k1_ctx);
}

//////////////////////////////
// PubKey methods
//////////////////////////////

PubKey::PubKey(const std::string& hex)
{
    std::array<uint8_t, 33> serialized;
    if (HexRef(hex).parse_to(serialized) && secp256k1_ec_pubkey_parse(secp256k1_ctx, &pubkey, serialized.data(), serialized.size()))
        return;
    throw Error(EBADPUBKEY);
}

bool PubKey::operator==(const PubKey& rhs) const
{
    return secp256k1_ec_pubkey_cmp(secp256k1_ctx, &pubkey, &rhs.pubkey) == 0;
}

Address PubKey::address()
{
    Address ret;
    auto serialized = serialize();
    auto sha = hashSHA256(serialized);
    ripemd160(sha.data(), sha.size(), ret.data());
    return ret;
}

std::array<uint8_t, 33> PubKey::serialize() const
{
    std::array<uint8_t, 33> ret;
    size_t len = ret.size();
    assert(secp256k1_ec_pubkey_serialize(secp256k1_ctx, ret.data(), &len, &pubkey,
        SECP256K1_EC_COMPRESSED));
    return ret;
}
std::string PubKey::to_string() const { return serialize_hex(serialize()); }

PubKey::PubKey(const RecoverableSignature& recsig, HashView hv)
{
    if (!secp256k1_ecdsa_recover(secp256k1_ctx, &pubkey, &recsig.recsig, hv.data()))
        throw Error(ECORRUPTEDSIG);
}

//////////////////////////////
// Key methods
//////////////////////////////

#include <algorithm>
#include <climits>
#include <functional>
#include <random>
PrivKey::PrivKey()
{
    // Be careful on exotic systems where std::random_device is not secure.
    std::independent_bits_engine<std::random_device, CHAR_BIT, uint8_t> e;
    do {
        std::generate(std::begin(keydata), std::end(keydata), std::ref(e));
    } while (!check(keydata.data()));
}

PrivKey::PrivKey(std::string_view key)
{
    if (!HexRef(key).parse_to(keydata) || check(keydata.data()) == false)
        throw Error(EBADPRIVKEY);
}

PrivKey::PrivKey(const uint8_t* pbegin, const uint8_t* pend)
{
    if (size_t(pend - pbegin) != keydata.size() || check(&pbegin[0]) == false)
        throw Error(EBADPRIVKEY);
    memcpy(keydata.data(), (unsigned char*)&pbegin[0], keydata.size());
}

bool operator==(const PrivKey& a, const PrivKey& b)
{
    return memcmp(a.keydata.data(), b.keydata.data(), b.keydata.size()) == 0;
}

std::string PrivKey::to_string() const { return serialize_hex(keydata); }

PubKey PrivKey::pubkey() const
{
    PubKey pk {};
    assert(secp256k1_ec_pubkey_create(secp256k1_ctx, &pk.pubkey, keydata.data()));
    return pk;
}
RecoverableSignature PrivKey::sign(HashView hv) const
{
    return RecoverableSignature(keydata.data(), hv);
}

bool PrivKey::check(const uint8_t* vch)
{
    return secp256k1_ec_seckey_verify(secp256k1_ctx, vch);
}

//////////////////////////////
// RecoverableSignature methods
//////////////////////////////

wrt::optional<RecoverableSignature> RecoverableSignature::from_view(View<65> v)
{
    RecoverableSignature res { RecoverableSignature() };
    if (res.construct(v))
        return res;
    return {};
}

bool RecoverableSignature::construct(View<65> v)
{
    int recid = v.data()[64];
    if (recid < 0 || recid > 3 || (secp256k1_ecdsa_recoverable_signature_parse_compact(secp256k1_ctx, &recsig, v.data(), recid) != 1) || (check() != true)) {
        return false;
    }
    return true;
}

RecoverableSignature::RecoverableSignature(View<65> v)
{
    if (!construct(v))
        throw Error(ECORRUPTEDSIG);
}

namespace {
std::array<uint8_t, 65> parse_sig(std::string_view sv)
{
    std::array<uint8_t, 65> out;
    if (!HexRef(sv).parse_to(out))
        throw Error(EPARSESIG);
    return out;
}
}

RecoverableSignature::RecoverableSignature(std::string_view sv)
    : RecoverableSignature(parse_sig(sv))
{
}

bool RecoverableSignature::check() // check for lower S
{
    secp256k1_ecdsa_signature sig;
    assert(secp256k1_ecdsa_recoverable_signature_convert(secp256k1_ctx, &sig,
        &recsig));
    int res = secp256k1_ecdsa_signature_normalize(secp256k1_ctx, nullptr, &sig);
    return res == 0;
}


void RecoverableSignature::write_to(uint8_t* out65) const
{
    int recid;
    int ret = secp256k1_ecdsa_recoverable_signature_serialize_compact(
        secp256k1_ctx, out65, &recid, &recsig);
    assert(ret);
    assert(recid != -1);
    out65[64] = recid;
}

std::string RecoverableSignature::to_string() const
{
    return serialize_hex(to_array());
}
PubKey RecoverableSignature::recover_pubkey(HashView hv) const
{
    return PubKey(*this, hv);
}

RecoverableSignature::RecoverableSignature(const uint8_t* keydata, HashView hv)
{
    int ret = secp256k1_ecdsa_sign_recoverable(
        secp256k1_ctx, &recsig, hv.data(), keydata,
        secp256k1_nonce_function_rfc6979, nullptr);
    assert(ret);
}

// Regression test for the block body merkle commitment.
//
// The merkle root in the header is the only thing that binds the body bytes to
// the proof of work, and it is the fault boundary between "wrong body" and
// "wrong header" during block download. It must therefore be *byte fixating*:
// no byte string other than the miner's may produce the same root.
//
// The test builds a v4 body, serializes it (mining side), parses the bytes back
// (validation side) and requires that
//   - the leaf list of both sides is identical and covers the expected ranges,
//   - re-serialization reproduces the exact same bytes,
//   - flipping any single byte of the body either breaks the parse or changes
//     the root.
#include "block/body/body.hpp"
#include "block/body/container.hpp"
#include "block/chain/height.hpp"
#include "block/version.hpp"
#include "crypto/crypto.hpp"
#include "crypto/hasher_sha256.hpp"
#include "spdlog/spdlog.h"
#include <cstdio>
#include <vector>

using namespace block;

namespace {

const NonzeroHeight testHeight { uint32_t(1000000) };

PinNonce make_pin_nonce_value()
{
    std::array<uint8_t, 8> b { 1, 0, 0, 0, 0, 0, 0, 0 };
    Reader r { std::span<const uint8_t> { b } };
    return PinNonce { r };
}

Address make_address(uint8_t seed)
{
    std::array<uint8_t, 20> a;
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = uint8_t(seed + i);
    return Address { std::span<const uint8_t, 20> { a } };
}

struct ExpectedRange {
    size_t offset;
    size_t length;
};

ParsedBody make_body(const RecoverableSignature& sig, bool with_empty_section)
{
    std::vector<Address> addresses { make_address(0xA0) };
    body::Reward reward { ToAccIdEl { AccountId { 1 } }, WartEl { Wart { 100 } } };

    body::Entries entries;
    const auto fee { CompactFeeEl { CompactUInt::from_value_assert(0) } };
    const auto pn { PinNonceEl { make_pin_nonce_value() } };

    entries.wart_transfers().try_push_back(
        body::WartTransfer { OriginAccIdEl { AccountId { 7 } }, pn, fee,
            ToAccIdEl { AccountId { 2 } }, WartEl { Wart { 55 } }, SignatureEl { sig } },
        BlockVersion::v4);

    Tag<"tokenSection", body::elements::tokens::TokenSection> section { AssetId { 5 } };
    section.liquidity_deposits().try_push_back(
        body::LiquidityDeposit { OriginAccIdEl { AccountId { 7 } }, pn, fee,
            BaseEl { Funds_uint64 { 20 } }, QuoteEl { Wart { 30 } }, SignatureEl { sig } },
        BlockVersion::v4);
    entries.tokens().push_back(std::move(section));

    if (with_empty_section) {
        Tag<"tokenSection", body::elements::tokens::TokenSection> empty { AssetId { 6 } };
        entries.tokens().push_back(std::move(empty));
    }
    return ParsedBody { std::move(addresses), std::move(reward), std::move(entries) };
}

std::optional<Hash> parse_root(const std::vector<uint8_t>& data)
{
    try {
        VersionedBodyData vd { BodyData { data }, BlockVersion::v4 };
        Body b { Body::parse_throw(std::move(vd), testHeight) };
        return b.merkle_root(testHeight);
    } catch (const Error&) {
        return {};
    }
}

bool check(bool condition, const char* what)
{
    if (!condition)
        std::printf("  FAIL: %s\n", what);
    return condition;
}

bool run(const char* name, ParsedBody body, const std::vector<ExpectedRange>& expected)
{
    std::printf("%s\n", name);
    bool ok = true;

    // mining side: serialize the body, the leaves become the header root
    Body written { Body::serialize(std::move(body)) };
    const std::vector<uint8_t> bytes { written.data };
    const auto& writeLeaves { written.merkleLeaves.hashes };
    const auto writeRoot { written.merkle_root(testHeight) };

    // validation side: parse exactly these bytes
    VersionedBodyData vd { BodyData { bytes }, BlockVersion::v4 };
    std::optional<Body> readOpt;
    try {
        readOpt.emplace(Body::parse_throw(std::move(vd), testHeight));
    } catch (const Error& e) {
        std::printf("  FAIL: parse threw %s\n", e.err_name());
        return false;
    }
    Body read { std::move(*readOpt) };

    ok &= check(writeRoot == read.merkle_root(testHeight), "read root differs from write root");
    ok &= check(writeLeaves == read.merkleLeaves.hashes, "leaf lists differ");

    // every body byte must be covered by exactly one of these leaves
    if (writeLeaves.size() != expected.size()) {
        std::printf("  FAIL: got %zu leaves, expected %zu\n", writeLeaves.size(), expected.size());
        ok = false;
    } else {
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& r { expected[i] };
            auto want { hashSHA256(bytes.data() + r.offset, r.length) };
            if (!(writeLeaves[i] == want)) {
                std::printf("  FAIL: leaf %zu does not cover bytes [%zu, %zu)\n",
                    i, r.offset, r.offset + r.length);
                ok = false;
            }
        }
    }

    // re-serializing what we parsed must reproduce the bytes
    Body again { Body::serialize(static_cast<ParsedBody>(read)) };
    ok &= check(std::vector<uint8_t>(again.data) == bytes, "re-serialized bytes differ");
    ok &= check(again.merkle_root(testHeight) == writeRoot, "re-serialized root differs");

    // byte fixation: no single byte may be changed without detection
    size_t undetected { 0 };
    for (size_t i = 0; i < bytes.size(); ++i) {
        auto mutated { bytes };
        mutated[i] ^= 0x01;
        auto r { parse_root(mutated) };
        if (r && *r == writeRoot) {
            std::printf("  FAIL: byte %zu can be flipped without changing the root\n", i);
            ++undetected;
            if (undetected > 4)
                break;
        }
    }
    ok &= check(undetected == 0, "body bytes are not covered by the merkle root");

    std::printf("  %zu bytes, %zu leaves, %s\n", bytes.size(), writeLeaves.size(),
        ok ? "OK" : "FAILED");
    return ok;
}

}

int main()
{
    spdlog::set_level(spdlog::level::warn);
    ECC_Start();
    const PrivKey key {};
    const RecoverableSignature sig { key.sign(Hash::zero()) };

    // leaf ranges:
    //   10 addressCount, 12 address, 32 reward,
    //   48 wartTransfers count, 52 wart transfer,
    //   151 cancelations count, 153 tokenSections count,
    //   155 assetId, 163 tenBitLengths, 170 liquidity deposit,
    //   269 assetCreations count
    const std::vector<ExpectedRange> oneSection {
        { 10, 2 }, { 12, 20 }, { 32, 16 }, { 48, 4 }, { 52, 99 },
        { 151, 2 }, { 153, 2 }, { 155, 8 }, { 163, 7 }, { 170, 99 }, { 269, 2 }
    };
    // plus a second, empty section: its assetId and its zeroed lengths still
    // have to be covered, otherwise empty sections could be injected
    const std::vector<ExpectedRange> twoSections {
        { 10, 2 }, { 12, 20 }, { 32, 16 }, { 48, 4 }, { 52, 99 },
        { 151, 2 }, { 153, 2 }, { 155, 8 }, { 163, 7 }, { 170, 99 },
        { 269, 8 }, { 277, 7 }, { 284, 2 }
    };

    bool ok = true;
    ok &= run("body with one token section", make_body(sig, false), oneSection);
    ok &= run("body with an empty second section", make_body(sig, true), twoSections);

    ECC_Stop();
    if (!ok)
        std::printf("FAILED\n");
    return ok ? 0 : 1;
}

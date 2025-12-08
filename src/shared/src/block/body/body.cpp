#include "body.hpp"
#include "block/body/container.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/is_testnet.hpp"
#include "general/writer.hpp"
namespace block {
namespace body {
namespace elements {
TokenSection::TokenSection(StructuredReader& r)
    : AssetIdElement(r.merkle_frame().reader)
    , TokenEntries(r)
{
}
}

auto ParsedBody::tx_ids(NonzeroHeight height, PinHeight minPinHeight) const -> BlockTxids
{
    BlockTxids res;

    for (auto& c : cancelations()) {
        // may produce duplacates at this point, later we will enforce that
        // blocks cannot include transactions which are canceled in the same
        // block
        auto cid { c.canceled_txid() };
        if (cid.pinHeight >= height)
            throw std::runtime_error("pinHeight > height for canceled transaction id");
        if (cid.pinHeight >= minPinHeight) {
            res.fromCancelations.push_back(cid);
        }
    }

    // read transaction ids of all entries in the block
    visit_signed_entries([&](auto&& entry) { entry.append_txids(res.fromTransactions, height.pin_floor(), minPinHeight); });

    return res;
}

std::pair<ParsedBody, MerkleLeaves> ParsedBody::parse_throw(std::span<const uint8_t> data, NonzeroHeight h, BlockVersion version, ParseAnnotations* parseAnnotations)
{
    if (data.size() > MAXBLOCKSIZE)
        throw Error(EINV_BODY);
    StructuredReader r(data, parseAnnotations);
    try {
        bool block_v2 = is_testnet() || h.value() >= NEWBLOCKSTRUCUTREHEIGHT;

        body_vector<Address> addresses;

        // read extra nonce
        {
            auto a { r.annotate("extraNonce") };
            r.skip(block_v2 ? 10 : 4); // for mining
        }

        {
            auto a { r.annotate("newAddresses") };
            // read number of addresses
            size_t len { [&]() -> size_t {
                auto a { r.annotate("length") };
                return block_v2 ? size_t(r.uint16()) : size_t(r.uint32());
            }() };

            // read addresses
            addresses = { len, r };
        }
        auto reward {
            [&]() {
                // create hook to auto-insert merkle entry
                auto hook { r.merkle_frame() };
                if (!block_v2) {
                    r.annotate("reserved(1)").reader.skip(2); // # of entries, which should be 1
                }
                return body::Reward { r.annotate("reward") };
            }()
        };
        body::Entries entries(r);
        entries.validate_version(version);
        return { ParsedBody { std::move(addresses), std::move(reward), std::move(entries) }, std::move(r).move_leaves() };
    } catch (const Error& e) {
        if (e.code == EMSGINTEGRITY)
            throw Error(EINV_BODY); // more meaningful error
        throw e;
    }
}

void ParsedBody::serialize(MerkleSerializer auto&& s) const
{
    s.writer.skip(10); // for mining

    // serialize new addresses with 16 bit length
    assert(std::numeric_limits<uint16_t>::max() >= newAddresses.size());
    s.writer << uint16_t(newAddresses.size());
    s << newAddresses
      << reward
      << entries();
}

SerializedBody ParsedBody::serialize_v4() const
{
    std::vector<uint8_t> res(count_bytes(*this), 0);
    Writer w(res);
    MerkleWriteHooker merkle(w);
    merkle << *this;

    assert(w.remaining() == 0);
    return { VersionedBodyData {
                 std::move(res),
                 BlockVersion::v4,
             },
        std::move(merkle).move_leaves() };
};

Body Body::parse_throw(VersionedBodyData c, NonzeroHeight h, ParseAnnotations* parseAnnotations)
{
    auto p { ParsedBody::parse_throw(c, h, c.version, parseAnnotations) };
    return Body(std::move(p.first), std::move(c), std::move(p.second));
}

Body Body::serialize(ParsedBody b)
{
    auto ser { b.serialize<BlockVersion::v4>() };
    return Body { std::move(b), std::move(ser.container), std::move(ser.merkleLeaves) };
}
}
}

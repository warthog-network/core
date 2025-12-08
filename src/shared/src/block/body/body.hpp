#pragma once
#include "block/body/body_fwd.hpp"
#include "block/body/container.hpp"
#include "block/body/elements.hpp"
#include "block/body/transaction_types.hpp"
#include "block/version.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/reader_declaration.hpp"
#include "general/structured_reader.hpp"
#include "general/writer_fwd.hpp"
#include "merkle_write_hooker.hpp"
#include <cstdint>

namespace block {
namespace body {

// Vector elements without serialization of length
template <typename T>
struct VectorEntries : public std::vector<T> {
    using elem_t = T;
    using std::vector<T>::vector;
    VectorEntries(std::vector<T> v)
        : std::vector<T>(std::move(v))
    {
    }
    auto& entries() const { return *this; }
    auto& entries() { return *this; }

    template <RawSerializer S>
    void serialize(S&& s) const
    {
        for (auto& e : *this)
            s << e;
    }

    template <MerkleSerializer S>
    void serialize(S&& s) const
    {
        for (auto& e : *this)
            s << e;
    }

    void append_txids(std::vector<TransactionId>& v, PinFloor pf, PinHeight minPinHeight) const
    {
        for (auto& e : *this)
            e.append_txids(v, pf, minPinHeight);
    }

    auto operator<=>(const VectorEntries<T>&) const = default;
    VectorEntries(Reader& r, size_t n)
    {
        for (size_t i { 0 }; i < n; ++i) {
            this->push_back(T(r));
        }
    }
    VectorEntries(size_t n, StructuredReader& m)
    {
        for (size_t i { 0 }; i < n; ++i) {
            this->push_back(T(m.merkle_frame().reader));
        }
    }
};

template <StaticString annotation, typename T>
struct TaggedVectorElements : public VectorEntries<T> {
    TaggedVectorElements()
    {
    }
    TaggedVectorElements(size_t n, StructuredReader& m)
        : VectorEntries<T>(n, m.annotate(annotation.to_string()).reader)
    {
    }
};

class NewAddresses {
public:
    NewAddresses(AccountId nextAccountId)
        : nextAccountId(nextAccountId)
    {
    }
    AccountId operator[](const Address& address)
    {
        auto [iter, inserted] { map.try_emplace(address, nextAccountId) };
        if (inserted)
            nextAccountId++;
        return iter->second;
    }
    auto get_vector() const
    {
        std::vector<Address> out;
        for (auto& [addr, _id] : map)
            out.push_back(addr);
        return out;
    };

private:
    std::map<Address, AccountId> map;
    AccountId nextAccountId;
};
namespace elements {

namespace tokens {

struct AssetTransfers : public IsTokenTransfer, public TaggedVectorElements<"assetTransfers", body::AssetTransfer> {
    using TaggedVectorElements::TaggedVectorElements;
    auto& asset_transfers() const { return entries(); }
    auto& asset_transfers() { return entries(); }
};
struct LiquidityTransfers : public IsTokenTransfer, public TaggedVectorElements<"shareTransfers", body::LiquidityTransfer> {
    using TaggedVectorElements::TaggedVectorElements;
    auto& liquidity_transfers() const { return entries(); }
    auto& liquidity_transfers() { return entries(); }
};
struct Orders : public IsLimitSwap, public TaggedVectorElements<"orders", body::Order> {
    using TaggedVectorElements::TaggedVectorElements;
    auto& orders() const { return entries(); }
    auto& orders() { return entries(); }
};
struct LiquidityDeposits : public IsLiquidityDeposit, public TaggedVectorElements<"liquidityDeposits", body::LiquidityDeposit> {
    using TaggedVectorElements::TaggedVectorElements;
    auto& liquidity_deposits() const { return entries(); }
    auto& liquidity_deposits() { return entries(); }
};
struct LiquidityWithdrawals : public IsLiquidityWithdrawal, public TaggedVectorElements<"liquidityWithdrawals", body::LiquidityWithdrawal> {
    using TaggedVectorElements::TaggedVectorElements;
    auto& liquidity_withdrawals() const { return entries(); }
    auto& liquidity_withdrawals() { return entries(); }
};

template <size_t N>
struct TenBitLengths {
    using arr_t = std::array<size_t, N>;

private:
    static constexpr size_t byte_size() { return (10 * N + 7) / 8; }
    template <size_t... Is>
    static auto read_data(std::index_sequence<Is...>, Reader& rd)
    {
        auto r10 { [bits = size_t(0), nbits = 0u, &rd]() mutable -> size_t {
            while (nbits < 10) {
                bits <<= 8;
                bits |= rd.uint8();
                nbits += 8;
            }
            auto excess { nbits - 10 };
            auto res { bits >> excess };
            bits &= ((1 << excess) - 1);
            nbits -= 10;
            return res;
        } };
        return arr_t { (void(Is), r10())... };
    }

public:
    TenBitLengths(StructuredReader& mr)
        : data([&]() {
        auto mf{mr.merkle_frame()};
        return read_data(std::make_index_sequence<N>(), mr.annotate("tenBitsLengths")); }())
    {
    }
    TenBitLengths(arr_t arr)
        : data { std::move(arr) }
    {
    }
    void serialize(RawSerializer auto&& s) const
    {
        uint32_t bits { 0 };
        size_t nbits { 0 };
        for (auto v : this->data) {
            bits |= uint32_t(v) << (22 - nbits);
            nbits += 10;
            while (nbits >= 8) {
                s << (uint8_t)(bits >> 24);
                bits <<= 8;
                nbits -= 8;
            }
        }
        if (nbits > 0)
            s << uint8_t(bits >> 24);
    }
    template <size_t i>
    requires(i < N)
    size_t at() const
    {
        return data[i];
    }

private:
    arr_t data;
};

template <typename... Ts>
struct TokenEntries : public Ts... {
private:
    using bits_t = TenBitLengths<sizeof...(Ts)>;
    template <size_t... Is>
    TokenEntries(std::index_sequence<Is...>, const bits_t& lengths, StructuredReader& m)
        : Ts(lengths.template at<Is>(), m)...
    {
    }

    template <typename... Rs>
    struct Overload : public Rs... {
        using Rs::operator()...;
    };
    template <typename T>
    auto& parent() const { return *static_cast<const T*>(this); }
    template <typename T>
    auto& parent() { return *static_cast<T*>(this); }

public:
    template <typename Lambda>
    requires(std::is_invocable_v<Lambda, const typename Ts::elem_t&> && ...)
    void visit_components(Lambda&& lambda) const
    {
        ([&](auto& entries) {
            for (auto& e : entries)
                lambda(e);
        }(static_cast<const Ts*>(this)->entries()),
            ...);
    }
    template <typename... Ls>
    requires(std::is_invocable_v<Ls, const typename Ts::elem_t&> && ...)
    void visit_components_overload(Ls&&... lambdas) const
    {
        visit_components(Overload { std::forward<Ls>(lambdas)... });
    }
    TokenEntries(StructuredReader& r)
        : TokenEntries { std::index_sequence_for<Ts...>(), bits_t(r), r }
    {
    }
    auto& token_entries() const { return *this; }
    void append_txids(std::vector<TransactionId>& v, PinFloor pf, PinHeight minPinHeight) const
    {
        (static_cast<const Ts*>(this)->append_txids(v, pf, minPinHeight), ...);
    }
    void serialize(MerkleSerializer auto&& s) const
    {
        typename bits_t::arr_t arr { static_cast<const Ts*>(this)->entries().size()... };
        s.writer << bits_t(arr);
        (s << ... << parent<Ts>());
    }

    TokenEntries() { }
};

struct AssetIdElement {
protected:
    AssetId assetId;

public:
    AssetIdElement(AssetId id)
        : assetId(id)
    {
    }
    AssetIdElement(StructuredReader& r)
        : assetId(r.annotate("assetId"))
    {
    }
    auto asset_id() const { return assetId; }
};

class TokenSection : public AssetIdElement, public TokenEntries<AssetTransfers, LiquidityTransfers, Orders, LiquidityDeposits, LiquidityWithdrawals> {
    struct Dummy { };

public:
    static constexpr const size_t n_vectors = 5;
    void append_tx_ids(PinFloor, std::vector<TransactionId>& appendTo) const;

    void serialize(MerkleSerializer auto&& s) const
    {
        s.writer << assetId;
        s << token_entries();
    }
    TokenSection(StructuredReader& m);
    TokenSection(AssetId tid)
        : AssetIdElement(tid) { };
    void append_txids(std::vector<TransactionId>& out, PinFloor pf, PinHeight minPinHeight) const
    {
        TokenEntries::append_txids(out, pf, minPinHeight);
    }
};
}

template <typename UInt, typename Elem>
struct UntaggedSizeVector : public VectorEntries<Elem> {

    using VectorEntries<Elem>::entries;
    void serialize(MerkleSerializer auto& w) const
    {
        w.writer << UInt(this->size());
        VectorEntries<Elem>::serialize(w);
    }
    UntaggedSizeVector() { }
    UntaggedSizeVector(StructuredReader& r)
        : VectorEntries<Elem> { [&]() {
            if (r.remaining() == 0) {
                return VectorEntries<Elem> {};
            } else {
                UInt len(r.annotate("length").reader);
                return VectorEntries<Elem> { len, r };
            };
        }() }
    {
    }
};
template <StaticString tag, typename UInt, typename Elem>
using SizeVector = Tag<tag, UntaggedSizeVector<UInt, Elem>>;

template <typename T>
void apply_to_entries(T&& t, auto&& lambda)
{
    lambda(t);
}

template <typename UInt, typename Elem>
void apply_to_entries(UntaggedSizeVector<UInt, Elem>& v, auto&& lambda)
{
    for (auto& e : v)
        apply_to_entries(e, lambda);
}

struct WartTransfers : public IsWartTransfer, public SizeVector<"wartTransfers", uint32_t, body::WartTransfer> {
    using Tag::Tag;
    auto& wart_transfers() const { return entries(); }
    auto& wart_transfers() { return entries(); }
};

struct TokenSections : public SizeVector<"tokenSections", uint16_t, Tag<"tokenSection", tokens::TokenSection>> {
    auto& tokens() const { return entries(); }
    auto& tokens() { return entries(); }
};

struct Cancelations : public IsCancelation, public SizeVector<"cancelations", uint16_t, body::Cancelation> {
    using SizeVector<"cancelations", uint16_t, body::Cancelation>::SizeVector;
    auto& cancelations() const { return entries(); }
    auto& cancelations() { return entries(); }
};

struct AssetCreations : public IsAssetCreate, public SizeVector<"assetCreations", uint16_t, body::AssetCreation> {
    using SizeVector<"assetCreations", uint16_t, body::AssetCreation>::SizeVector;
    auto& asset_creations() const { return entries(); }
    auto& asset_creations() { return entries(); }
};

template <typename... Ts>
struct CombineElements : public Ts... {
private:
    template <typename... Rs>
    struct Overload : public Rs... {
        using Rs::operator()...;
    };

public:
    CombineElements(StructuredReader& r)
        : Ts(r)...
    {
    }
    CombineElements() { }

    template <typename Lambda>
    requires(std::is_invocable_v<Lambda, const typename Ts::elem_t&> && ...)
    void visit_components(Lambda&& lambda) const
    {
        ([&](auto& entries) {
            for (auto& e : entries)
                lambda(e);
        }(static_cast<const Ts*>(this)->entries()),
            ...);
    }
    template <typename... Ls>
    requires(std::is_invocable_v<Ls, const typename Ts::elem_t&> && ...)
    void visit_components_overload(Ls&&... lambdas) const
    {
        visit_components(Overload { std::forward<Ls>(lambdas)... });
    }

    template <typename Lambda>
    void visit_signed_entries(Lambda&& lambda) const
    {
        visit_components([&](auto& entry) { apply_to_entries(entry, lambda); });
    }

    void serialize(MerkleSerializer auto& s) const
    {
        (s << ... << *static_cast<const Ts*>(this));
    }
    void append_txids(std::vector<TransactionId>& v, PinFloor pf, PinHeight minPinHeight) const
    {
        (static_cast<const Ts*>(this)->append_txids(v, pf, minPinHeight), ...);
    }
};

struct Entries : public CombineElements<WartTransfers, Cancelations, TokenSections, AssetCreations> {
    using CombineElements::CombineElements;
    Entries& entries() { return *this; }
    const Entries& entries() const { return *this; }
    void validate_version(BlockVersion version)
    {
        if (version.value() < 4) {
            if (!cancelations().empty() || !tokens().empty() || !asset_creations().empty()) {
                throw Error(EBLOCKV4);
            }
        };
    }
};
}

struct SerializedBody {
    VersionedBodyData container;
    MerkleLeaves merkleLeaves;
    Hash merkle_root(NonzeroHeight h) const { return merkleLeaves.merkle_root(container, h); }
};

struct AddressReward {
    AddressReward(std::vector<Address> addresses, body::Reward reward)
        : reward(std::move(reward))
    {
        for (auto& a : addresses)
            newAddresses.push_back(std::move(a));
    }
    VectorEntries<HookMerkle<Tag<"Address", Address>>> newAddresses;
    body::Reward reward;
};
using elements::Entries;
using elements::tokens::TokenSection;

class ParsedBody : public AddressReward, public Entries {
private:
    template <typename T>
    using body_vector = block::body::VectorEntries<T>;
    [[nodiscard]] SerializedBody serialize_v4() const;

public:
    ParsedBody(std::vector<Address> newAddresses, Reward reward, Entries entries)
        : AddressReward(std::move(newAddresses), std::move(reward))
        , Entries(std::move(entries))
    {
    }
    struct BlockTxids {
        std::vector<TransactionId> fromTransactions;
        std::vector<TransactionId> fromCancelations;
    };
    BlockTxids tx_ids(NonzeroHeight height, PinHeight minPinHeight) const;
    [[nodiscard]] static std::pair<ParsedBody, MerkleLeaves> parse_throw(std::span<const uint8_t> rd, NonzeroHeight h, BlockVersion version, ParseAnnotations* = nullptr);

    void serialize(MerkleSerializer auto&& s) const;

    template <BlockVersion blockVersion>
    [[nodiscard]] SerializedBody serialize() const
    {
        static_assert(blockVersion == BlockVersion::v4);
        return serialize_v4();
    }
};

struct Body : public ParsedBody {
    VersionedBodyData data;
    MerkleLeaves merkleLeaves;
    [[nodiscard]] static Body parse_throw(VersionedBodyData c, NonzeroHeight h, ParseAnnotations* parseAnnotations = nullptr);
    [[nodiscard]] static Body serialize(ParsedBody);
    auto merkle_root(NonzeroHeight h) const
    {
        return merkleLeaves.merkle_root(data, h);
    }
    auto merkle_prefix() const
    {
        return merkleLeaves.merkle_prefix();
    }

private:
    Body(ParsedBody parsed, VersionedBodyData raw, MerkleLeaves merkleTree)
        : ParsedBody(std::move(parsed))
        , data(std::move(raw))
        , merkleLeaves(std::move(merkleTree))
    {
    }
};
}
}

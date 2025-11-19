#pragma once
#include "block/body/body_fwd.hpp"
#include "block/body/container.hpp"
#include "block/body/elements.hpp"
#include "block/version.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/reader_declaration.hpp"
#include "general/structured_reader.hpp"
#include "general/writer_fwd.hpp"
#include <cstdint>

namespace block {
namespace body {

struct MerkleWriteHooker;
struct MerkleWriteHook {
public:
    operator Writer&()
    {
        return writer;
    }

    MerkleWriteHook(MerkleWriteHook&& mi)
        : MerkleWriteHook(mi.writer, mi.creator)
    {
        mi.begin = nullptr;
    }

    ~MerkleWriteHook();

private:
    friend MerkleWriteHooker;
    MerkleWriteHook(Writer& w, MerkleWriteHooker& c);
    MerkleWriteHook(const MerkleWriteHook& mi) = delete;

public:
    Writer& writer;

private:
    MerkleWriteHooker& creator;
    const uint8_t* begin;
};
struct MerkleWriteHooker {
    friend MerkleWriteHook;
    MerkleWriteHooker(Writer& w)
        : writer(w)
    {
    }
    [[nodiscard]] MerkleWriteHook hook();
    MerkleLeaves move_leaves() && { return std::move(leaves); }

private:
    void add_hash_of(const std::span<const uint8_t>& s)
    {
        leaves.add_hash(hashSHA256(s));
    }

public:
    Writer& writer;

private:
    MerkleLeaves leaves;
};

template <typename T>
struct vector : public std::vector<T> {
    using elem_t = T;
    using std::vector<T>::vector;
    vector(std::vector<T> v)
        : std::vector<T>(std::move(v))
    {
    }
    auto& get() const { return *this; }
    auto& get() { return *this; }
    void serialize(Serializer auto&& s) const
    {
        for (auto& e : *this)
            s << e;
    }

    template <typename uint_t>
    void write(MerkleWriteHooker& m) const
    {
        m.writer << uint_t(this->size());
        for (auto& e : *this) {
            auto h { m.hook() };
            h.writer << e;
        }
    }

    void append_txids(std::vector<TransactionId>& v, PinFloor pf, PinHeight minPinHeight) const
    {
        for (auto& e : *this)
            e.append_txids(v, pf, minPinHeight);
    }

    auto operator<=>(const vector<T>&) const = default;

    size_t byte_size() const
    {
        size_t i { 0 };
        for (auto& elem : *this)
            i += elem.byte_size();
        return i;
    }
    vector(size_t n, Reader& r)
    {
        for (size_t i { 0 }; i < n; ++i) {
            this->push_back(T(r));
        }
    }
    vector(size_t n, StructuredReader& m)
    {
        for (size_t i { 0 }; i < n; ++i) {
            this->push_back(T(m.merkle_frame().reader));
        }
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
    std::vector<Address> get_vector() const
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

struct AssetTransfers : public Tag<"assetTransfers", vector<body::AssetTransfer>> {
    using Tag::Tag;
    auto& asset_transfers() const { return get(); }
    auto& asset_transfers() { return get(); }
};
struct ShareTransfers : public Tag<"shareTransfers", vector<body::LiquidityTransfer>> {
    using Tag::Tag;
    auto& share_transfers() const { return get(); }
    auto& share_transfers() { return get(); }
};
struct Orders : public Tag<"orders", vector<body::Order>> {
    using Tag::Tag;
    auto& orders() const { return get(); }
    auto& orders() { return get(); }
};
struct LiquidityDeposits : public Tag<"liquidityDeposits", vector<body::LiquidityDeposit>> {
    using Tag::Tag;
    auto& liquidity_deposits() const { return get(); }
    auto& liquidity_deposits() { return get(); }
};
struct LiquidityWithdrawals : public Tag<"liquidityWithdrawals", vector<body::LiquidityWithdrawal>> {
    using Tag::Tag;
    auto& liquidity_withdrawals() const { return get(); }
    auto& liquidity_withdrawals() { return get(); }
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
    void serialize(Serializer auto&& s) const
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

public:
    template <typename Lambda>
    requires(std::is_invocable_v<Lambda, const typename Ts::elem_t&> && ...)
    void visit_components(Lambda&& lambda) const
    {
        ([&](auto& entries) {
            for (auto& e : entries)
                lambda(e);
        }(static_cast<const Ts*>(this)->get()),
            ...);
    }
    template <typename... Ls>
    requires(std::is_invocable_v<Ls, const typename Ts::elem_t&> && ...)
    void visit_components_overload(Ls&&... lambdas) const
    {
        visit_components(Overload { std::forward<Ls>(lambdas)... });
    }
    TokenEntries(StructuredReader& mr)
        : TokenEntries { std::index_sequence_for<Ts...>(), bits_t(mr), mr }
    {
    }
    auto& token_entries() const { return *this; }
    [[nodiscard]] size_t byte_size() const
    {
        return (static_cast<const Ts*>(this)->byte_size() + ...);
    }
    void serialize(Serializer auto&& s) const
    {
        typename TenBitLengths<sizeof...(Ts)>::arr_t arr { static_cast<const Ts*>(this)->get().size()... };
        s << bits_t(arr);
        (s << ... << *static_cast<const Ts*>(this));
    }
    void append_txids(std::vector<TransactionId>& v, PinFloor pf, PinHeight minPinHeight) const
    {
        (static_cast<const Ts*>(this)->append_txids(v, pf, minPinHeight), ...);
    }
    void write(MerkleWriteHooker& m) const
    {
        auto write_hooked { [&](auto& arg) {
            auto h { m.hook() };
            h.writer << arg;
        } };
        (write_hooked(*static_cast<const Ts*>(this)), ...);
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

class TokenSection : public AssetIdElement, public TokenEntries<AssetTransfers, ShareTransfers, Orders, LiquidityDeposits, LiquidityWithdrawals> {
    struct Dummy { };

    TokenSection(StructuredReader& m, Dummy); // different signature to delegate to (for annotation)

public:
    static constexpr const size_t n_vectors = 5;
    void append_tx_ids(PinFloor, std::vector<TransactionId>& appendTo) const;

    void write(MerkleWriteHooker& w);
    TokenSection(StructuredReader& m)
        : TokenSection(m.annotate("tokenSection"), {})
    {
    }
    TokenSection(AssetId tid)
        : AssetIdElement(tid) { };
    void append_txids(std::vector<TransactionId>& out, PinFloor pf, PinHeight minPinHeight) const
    {
        TokenEntries::append_txids(out, pf, minPinHeight);
    }
    size_t byte_size() const
    {
        return assetId.byte_size() + TokenEntries::byte_size();
    };
};
}

template <typename UInt, typename Elem>
struct UntaggedSizeVector : public vector<Elem> {
    [[nodiscard]] size_t byte_size() const
    {
        return sizeof(UInt) + this->get().byte_size();
    }
    void write(MerkleWriteHooker& w) const
    {
        vector<Elem>::template write<UInt>(w);
    }
    UntaggedSizeVector() { }
    UntaggedSizeVector(StructuredReader& r)
        : vector<Elem> { [&]() {
            if (r.remaining() == 0) {
                return vector<Elem> {};
            } else {
                UInt len(r.annotate("length").reader);
                return vector<Elem> { len, r };
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

struct WartTransfers : public SizeVector<"wartTransfers", uint32_t, body::WartTransfer> {
    using Tag::Tag;
    auto& wart_transfers() const { return get(); }
    auto& wart_transfers() { return get(); }
};

struct TokenSections : public SizeVector<"tokenSections", uint16_t, tokens::TokenSection> {
    auto& tokens() const { return get(); }
    auto& tokens() { return get(); }
};

struct Cancelations : public SizeVector<"cancelations", uint16_t, body::Cancelation> {
    auto& cancelations() const { return get(); }
    auto& cancelations() { return get(); }
};

struct AssetCreations : public SizeVector<"assetCreations", uint16_t, body::AssetCreation> {
    auto& asset_creations() const { return get(); }
    auto& asset_creations() { return get(); }
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
        }(static_cast<const Ts*>(this)->get()),
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

    [[nodiscard]] size_t byte_size() const
    {
        return (static_cast<const Ts*>(this)->byte_size() + ...);
    }
    void write(MerkleWriteHooker& m) const
    {
        (static_cast<const Ts*>(this)->write(m), ...);
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
    AddressReward(std::vector<Address> newAddresses, body::Reward reward)
        : newAddresses(std::move(newAddresses))
        , reward(std::move(reward))
    {
    }
    vector<Address> newAddresses;
    body::Reward reward;
};
using elements::Entries;
using elements::tokens::TokenSection;

class ParsedBody : public AddressReward, public Entries {
private:
    template <typename T>
    using body_vector = block::body::vector<T>;

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
    size_t byte_size() const;
    [[nodiscard]] SerializedBody serialize() const;
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

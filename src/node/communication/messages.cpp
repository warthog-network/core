#include "messages.hpp"
#include "block/chain/worksum.hpp"
#include "block/header/view.hpp"
#include "communication/buffers/sndbuffer.hpp"
#include "communication/messages.hpp"
#include "eventloop/eventloop.hpp"
#include "eventloop/types/chainstate.hpp"
#include "general/errors.hpp"
#include "general/reader.hpp"
#include "general/writer.hpp"
#include "mempool/entry.hpp"
#include <random>
#include <tuple>

namespace {
struct MessageWriter {
    MessageWriter(uint8_t msgtype, size_t msglen)
        : sb(msgtype, msglen)
        , writer(sb.msgdata(), sb.msgdata() + sb.msgsize()) {}
    operator Sndbuffer()
    {
        assert(writer.remaining() == 0);
        return std::move(sb);
    }
    MessageWriter& operator<<(bool b)
    {
        writer << (b ? uint8_t(1) : uint8_t(0));
        return *this;
    }
    template <typename T>
    MessageWriter& operator<<(const T& b)
    {
        writer << b;
        return *this;
    }
    MessageWriter& operator<<(const Range& r)
    {
        writer << r;
        return *this;
    }

    MessageWriter& operator<<(const Worksum& worksum)
    {
        for (uint32_t fr : worksum.getFragments()) {
            *this << fr;
        }
        return *this;
    }

private:
    Sndbuffer sb;
    Writer writer;
};
void throw_if_inconsistent(Height length, Worksum worksum)
{
    if ((length == 0) != worksum.is_zero()) {
        if (length == 0)
            throw Error(EFAKEWORK);
        throw Error(EFAKEHEIGHT);
    }
}
} // namespace

BatchSelector::BatchSelector(Reader& r)
    : descriptor(r.uint32())
    , startHeight(Height(r).nonzero_throw(EBATCHHEIGHT))
    , length(r) {}
template <uint8_t M>
MessageWriter MsgCode<M>::gen_msg(size_t len)
{
    return { msgcode, len };
}

RandNonce::RandNonce()
    : WithNonce { uint32_t(std::random_device{}()) } {}
InitMsg::InitMsg(Reader& r)
    : descriptor(r.uint32())
    , sp { r.uint16(), Height(r.uint32()) }
    , chainLength(r.uint32())
    , worksum(r.worksum())
    , grid(r.span())
{
    if (grid.slot_end().upper() <= chainLength)
        throw Error(EGRIDMISMATCH);
    throw_if_inconsistent(chainLength, worksum);
}

Sndbuffer InitMsg::serialize_chainstate(const ConsensusSlave& cs)
{
    const size_t N = cs.headers().complete_batches().size();
    size_t len = 4 + 2 + 4 + 4 + 32 + 4 + N * 80;
    auto& sp { cs.get_signed_snapshot_priority() };
    auto mw { gen_msg(len) };
    mw << cs.descriptor()
       << sp.importance
       << sp.height
       << cs.headers().length()
       << cs.total_work()
       << (uint32_t)(cs.headers().complete_batches().size() * 80)
       << Range(cs.grid().raw());
    return mw;
}

ForkMsg::ForkMsg(Descriptor descriptor, NonzeroHeight chainLength, Worksum worksum, NonzeroHeight forkHeight, Grid grid)
    : descriptor(descriptor)
    , chainLength(chainLength)
    , worksum(worksum)
    , forkHeight(forkHeight)
    , grid(grid)
{
    throw_if_inconsistent(chainLength, worksum);
}
auto ForkMsg::from_reader(Reader& r) -> ForkMsg
{
    return ForkMsg {
        r.uint32(),
        Height(r.uint32()).nonzero_throw(EZEROHEIGHT),
        r.worksum(),
        Height(r.uint32()).nonzero_throw(EFORKHEIGHT),
        r.rest()
    };
}

ForkMsg::operator Sndbuffer() const
{
    // assert(grid.size() > 0); // because it is a fork
    return gen_msg(4 + 4 + 32 + 4 + grid.raw().size())
        << descriptor << chainLength << worksum << forkHeight
        << grid.raw();
}

AppendMsg::AppendMsg(NonzeroHeight newLength,
    Worksum worksum,
    Grid grid)
    : newLength(newLength)
    , worksum(std::move(worksum))
    , grid(std::move(grid))
{
    throw_if_inconsistent(newLength, worksum);
}
auto AppendMsg::from_reader(Reader& r) -> AppendMsg
{
    return {
        r,
        r.worksum(),
        r.rest()
    };
}

AppendMsg::operator Sndbuffer() const
{
    return gen_msg(4 + 32 + grid.raw().size())
        << newLength
        << worksum
        << grid.raw();
}

auto SignedPinRollbackMsg::from_reader(Reader& r) -> SignedPinRollbackMsg
{
    return {
        r,
        r,
        r.worksum(),
        r.uint32()
    };
}

SignedPinRollbackMsg::operator Sndbuffer() const
{
    return gen_msg(signedSnapshot.binary_size + 4 + 32 + 4)
        << signedSnapshot
        << shrinkLength
        << worksum
        << descriptor;
}

auto PingMsg::from_reader(Reader& r) -> PingMsg
{
    return PingMsg {
        r.uint32(),
        r,
        r.uint16(),
        r.uint16()
    };
}
// : RandNonce(r.uint32()),
// sp { r.uint16(), Height(r.uint32()) }, maxAddresses(r.uint16()), maxTransactions(r.uint16()) {};

PingMsg::operator Sndbuffer() const
{
    return gen_msg(4 + 2 + 4 + 2 + 2)
        << nonce
        << sp.importance
        << sp.height
        << maxAddresses
        << maxTransactions;
}

auto PongMsg::from_reader(Reader& r) -> PongMsg
{
    auto nonce { r.uint32() };
    auto nAddresses = r.uint16();
    std::vector<EndpointAddress> addresses;
    for (size_t i = 0; i < nAddresses; ++i) {
        addresses.push_back({ r });
    }
    auto nTransactions { r.uint16() };
    std::vector<TxidWithFee> txids;
    for (size_t i = 0; i < nTransactions; ++i) {
        TransactionId txid { r };
        CompactUInt fee { CompactUInt::from_value_throw(r.uint16()) };
        txids.push_back(TxidWithFee { txid, fee });
    }
    return {
        nonce, std::move(addresses), std::move(txids)
    };
}

Error PongMsg::check(const PingMsg& m) const
{
    if (nonce != m.nonce)
        return EUNREQUESTED;
    if (addresses.size() > m.maxAddresses
        || txids.size() > m.maxTransactions) {
        return ERESTRICTED;
    }
    return 0;
}

PongMsg::operator Sndbuffer() const
{
    assert(addresses.size() < std::numeric_limits<uint16_t>::max());
    assert(txids.size() < std::numeric_limits<uint16_t>::max());
    uint16_t nAddresses = addresses.size();
    uint16_t nTxs = txids.size();

    auto mw { gen_msg(4 + 2 + 6 * nAddresses + 2 + 18 * nTxs) };
    mw << nonce
       << (uint16_t)nAddresses;
    for (auto& v : addresses) {
        mw << (uint32_t)v.ipv4.data
           << (uint16_t)v.port;
    };
    mw << (uint16_t)nTxs;
    for (auto& t : txids) {
        mw << t.txid
           << (uint16_t)t.fee.value();
    }
    return mw;
}

std::string BatchreqMsg::log_str() const
{
    return "batchreq [" + std::to_string(selector.startHeight) + "," + std::to_string(selector.startHeight + selector.length - 1) + "]";
}

auto BatchreqMsg::from_reader(Reader& r) -> BatchreqMsg
{
    return { r.uint32(), r };
}

BatchreqMsg::operator Sndbuffer() const
{
    assert(selector.startHeight != 0);
    auto& s = selector;
    return gen_msg(4 + 4 + 4 + 2)
        << nonce
        << s.descriptor
        << s.startHeight
        << s.length;
}

std::string ProbereqMsg::log_str() const
{
    return "probereq " + std::to_string(descriptor.value()) + "/" + std::to_string(height);
}

auto ProbereqMsg::from_reader(Reader& r) -> ProbereqMsg
{
    return { r.uint32(), r.uint32(), Height(r).nonzero_throw(EPROBEHEIGHT) };
}

ProbereqMsg::operator Sndbuffer() const
{
    return gen_msg(12) << nonce << descriptor << height;
}

auto ProberepMsg::from_reader(Reader& r) -> ProberepMsg
{
    auto nonce = r.uint32();
    auto currentDescriptor = r.uint32();

    std::optional<Header> requested;
    std::optional<Header> current;
    uint8_t type = r.uint8();
    if (type > 3)
        throw Error(EINV_PROBE);
    if ((type & 1) > 0) {
        requested = r.view<HeaderView>();
    }
    if ((type & 2) > 0) {
        current = r.view<HeaderView>();
    }
    return { nonce, currentDescriptor, requested, current };
}

ProberepMsg::operator Sndbuffer() const
{
    size_t slotsize = 0;
    uint8_t type = 0;
    if (requested.has_value()) {
        slotsize += requested.value().byte_size();
        type += 1;
    }
    if (current.has_value()) {
        slotsize += current.value().byte_size();
        type += 2;
    }

    auto w { gen_msg(9 + slotsize) };
    w << nonce << currentDescriptor << type;
    if (requested.has_value()) {
        w << *requested;
    }
    if (current.has_value()) {
        w << *current;
    }
    return w;
}

auto BatchrepMsg::from_reader(Reader& r) -> BatchrepMsg
{
    return { r.uint32(), r.rest() };
}

BatchrepMsg::operator Sndbuffer() const
{
    return gen_msg(4 + batch.raw().size())
        << nonce << Range(batch.raw());
}

std::string BlockreqMsg::log_str() const
{
    return "blockreq [" + std::to_string(range.lower) + "," + std::to_string(range.upper) + "]";
}

BlockreqMsg::operator Sndbuffer() const
{
    return gen_msg(16)
        << nonce
        << range;
}

auto BlockreqMsg::from_reader(Reader& r) -> BlockreqMsg
{
    return { r.uint32(), r };
}

auto BlockrepMsg::from_reader(Reader& r) -> BlockrepMsg
{
    // return
    auto nonce = r.uint32();
    std::vector<BodyContainer> bodies;
    while (r.remaining() != 0) {
        bodies.push_back({ r });
    }
    return { nonce, std::move(bodies) };
}

BlockrepMsg::operator Sndbuffer() const
{
    size_t size = 0;
    for (auto& b : blocks) {
        size += b.serialized_size();
    }
    auto mw { gen_msg(4 + size) };
    mw << nonce;
    for (auto& b : blocks)
        mw << b;

    return mw;
}

auto TxsubscribeMsg::from_reader(Reader& r) -> TxsubscribeMsg
{
    return {
        r.uint32(), r
    };
}

TxsubscribeMsg::operator Sndbuffer() const
{
    return gen_msg(8) << nonce << upper;
}

Sndbuffer TxnotifyMsg::direct_send(send_iter begin, send_iter end)
{
    auto mw { gen_msg(4 + (end - begin) * TransactionId::bytesize) };
    mw << RandNonce().nonce;
    for (auto iter = begin; iter != end; ++iter) {
        mw << iter->first;
    }
    return mw;
}

auto TxnotifyMsg::from_reader(Reader& r) -> TxnotifyMsg
{
    auto nonce = r.uint32();
    auto nTransactions { r.uint16() };

    std::vector<TxidWithFee> txids;
    for (size_t i = 0; i < nTransactions; ++i) {
        auto txid { r };
        auto fee { CompactUInt::from_value_throw(r.uint16()) };
        txids.push_back(TxidWithFee { txid, fee });
    }
    return { nonce, txids };
}

TxnotifyMsg::operator Sndbuffer() const
{
    assert(txids.size() <= std::numeric_limits<uint16_t>::max());
    assert(this->txids.size() > 0);
    auto mw { gen_msg(4 + txids.size() * TransactionId::bytesize) };
    mw << nonce;
    assert(txids.size() <= std::numeric_limits<uint16_t>::max());
    mw << (uint16_t)txids.size();
    for (auto& t : txids) {
        mw << t.txid
           << t.fee;
    }
    return mw;
}

auto TxreqMsg::from_reader(Reader& r) -> TxreqMsg
{
    auto nonce = r.uint32();
    std::vector<TransactionId> txids;
    while (r.remaining() >= TransactionId::bytesize) {
        txids.push_back(r);
        if (txids.size() > MAXENTRIES)
            throw Error(EINV_TXREQ);
    }
    return { nonce, std::move(txids) };
}

TxreqMsg::operator Sndbuffer() const
{
    assert(this->txids.size() > 0);
    auto mw { gen_msg(4 + txids.size() * TransactionId::bytesize) };
    mw << nonce;
    for (auto& txid : txids) {
        mw << txid;
    }
    return mw;
}

auto TxrepMsg::from_reader(Reader& r) -> TxrepMsg
{
    auto nonce = r.uint32();
    std::vector<std::optional<TransferTxExchangeMessage>> txs;
    while (r.remaining() > 0) {
        uint8_t indicator = r.uint8();
        if (indicator) {
            txs.push_back(TransferTxExchangeMessage { r });
        } else {
            txs.push_back({});
        }
        if (txs.size() > TxreqMsg::MAXENTRIES)
            throw Error(EINV_TXREP);
    }
    return { nonce, std::move(txs) };
}

TxrepMsg::operator Sndbuffer() const
{
    assert(this->txs.size() > 0);
    // compute length of values;
    size_t vals = 0;
    size_t novals = 0;
    for (auto& tx : txs) {
        if (tx) {
            vals += 1;
        } else {
            novals += 1;
        }
    }
    size_t vals_len = vals * (1 + TransferTxExchangeMessage::bytesize);
    size_t len = 4 + (novals * 1 + vals_len);

    auto m { gen_msg(len) };
    m << nonce;
    for (auto& tx : txs) {
        if (tx) {
            m << uint8_t(1) << *tx;
        } else {
            m << uint8_t(0);
        }
    }
    return m;
}
auto LeaderMsg::from_reader(Reader& r) -> LeaderMsg
{
    return { r };
}

LeaderMsg::operator Sndbuffer() const
{
    return gen_msg(signedSnapshot.binary_size)
        << signedSnapshot;
}

namespace {
template <uint8_t prevcode>
size_t size_bound(uint8_t)
{
    return 0;
}
template <uint8_t prevcode, typename T, typename... S>
size_t size_bound(uint8_t type)
{
    // variant types must be in order and message codes must be all different
    static_assert(prevcode < T::msgcode);
    if (T::msgcode == type)
        return T::maxSize;
    return size_bound<T::msgcode, S...>(type);
}

template <typename T, typename... S>
size_t size_bound(uint8_t type)
{
    if (T::msgcode == type)
        return T::maxSize;
    return size_bound<T::msgcode, S...>(type);
}

#include <variant>

// do metaprogramming dance
template <typename T>
class TypeExtractor {
};

template <typename... Types>
class TypeExtractor<std::variant<Types...>> {
public:
    static auto size_bound(uint8_t type)
    {
        return ::size_bound<Types...>(type);
    }
};

}

namespace messages {
size_t size_bound(uint8_t msgtype)
{
    return TypeExtractor<messages::Msg>::size_bound(msgtype);
}
}

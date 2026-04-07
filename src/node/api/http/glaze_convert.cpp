#include "glaze_convert.hpp"
#include "api/types/all.hpp"
#include "block/header/header_impl.hpp"
#include "chainserver/transaction_ids.hpp"
#include "crypto/hash.hpp"
#include "general/funds.hpp"
#include "general/hex.hpp"
#include "general/is_testnet.hpp"
#include "global/globals.hpp"
#include "transport/helpers/peer_addr.hpp"
#include <ranges>

namespace api {
namespace glaze {

namespace {
std::string format_utc(uint32_t timestamp)
{
    std::chrono::system_clock::time_point tp { std::chrono::seconds(timestamp) };
    auto tt { std::chrono::system_clock::to_time_t(tp) };
    auto utc_f = *std::gmtime(&tt);
    std::string out;
    out.resize(30);
    auto len { std::strftime(&out[0], out.size(), "%F %T UTC", &utc_f) };
    out.resize(len);
    return out;
}
Timepoint make_timepoint(uint32_t ts)
{
    return {
        .UTC = format_utc(ts),
        .timestamp = ts,
    };
}

FundsDecimal from(::FundsDecimal fd)
{
    return {
        .str = fd.to_string(),
        .u64 = fd.funds.value(),
        .decimals = fd.decimals.value()
    };
}
std::string from(const ::Peeraddr a)
{
    return a.to_string();
}
Wart from(const ::Wart w)
{
    return {
        .str { w.to_string() },
        .E8 = w.E8(),
    };
}

WartBalance from(const api::WartBalance& w)
{
    return {
        .total = from(w.total),
        .locked = from(w.locked),
        .mempool = from(w.mempool)
    };
}
FundsBalance from(const ::api::FundsBalance& fb)
{
    return {
        .total = from(fb.total),
        .locked = from(fb.locked),
        .mempool = from(fb.mempool)
    };
}
}
BlockHeader make_header(const ::Header& h, NonzeroHeight height);

std::string from(const Address& a)
{
    return a.to_string();
}

TransactionId from(const ::TransactionId txid)
{
    return {
        .accountId = from(txid.accountId),
        .nonceId = from(txid.nonceId),
        .pinHeight = from(txid.pinHeight),
    };
}
uint32_t from(const TokenDecimals& d)
{
    return d.value();
}
AssetBasic from(const ::AssetBasic& a)
{
    return {
        .hash { serialize_hex(a.hash) },
        .id = from(a.id),
        .name { a.name.to_string() },
        .decimals = from(a.decimals)
    };
}
TransactionSignedCommon from(const api::block::TransactionSignedData& d)
{
    return {
        .originId = from(d.originId),
        .originAddress { serialize_hex(d.originAddress) },
        .fee { from(d.fee) },
        .nonceId = from(d.nonceId),
        .pinHeight = from(d.pinHeight),
    };
}
static PriceDetail make_price(Price_uint64 p, TokenDecimals d)
{
    return {
        .precExponent10 = p.base10_decimals_exponent(d),
        .exponent2 = p.mantissa_exponent2(),
        .mantissa = p.mantissa_16bit(),
        .hex { serialize_hex(to_bytes(p)) },
        .doubleAdjusted = p.to_double_adjusted(d),
        .doubleRaw = p.to_double_raw(),
    };
}
uint64_t from(const IsUint64& i)
{
    return i.value();
}

uint32_t from(const IsUint32& i)
{
    return i.value();
}

template <typename T>
auto from(const api::block::WithHistoryId<T>& tx)
{
    return WithHistoryId { .transaction = from(tx.transaction), .historyId = tx.historyId.value() };
}

static BaseQuote make_base_quote(const defi::BaseQuote& bq, TokenDecimals d)
{
    return {
        .base { from(::FundsDecimal(bq.base(), d)) },
        .quote = from(bq.quote())
    };
}

Grid from(const ::Grid& g)
{
    Grid out;
    for (auto header : g) {
        out.headers.push_back(serialize_hex(header));
    }
    return out;
}

HashResult from(const ::Hash& h)
{
    return { .hash = serialize_hex(h) };
}

BanEntry from(const ::PeerDB::BanEntry& e)
{
    return {
        .ip = e.ip.to_string(),
        .banuntil = e.banuntil,
        .reason = e.offense.err_name()
    };
}
Account from(const ::api::Account& a)
{
    return {
        .address = a.address.to_string(),
        .accountId = a.id.value()
    };
}
ThrottledPeer from(const ::api::ThrottledPeer& p)
{
    using namespace std::chrono;
    return {
        .throttle {
            .delay = int(duration_cast<seconds>(p.throttle.delay).count()),
            .blockRequest {
                .h0 = p.throttle.blockreq.h0.value(),
                .h1 = p.throttle.blockreq.h1.value(),
                .window = p.throttle.batchreq.window },
            .headerRequest {
                .h0 = p.throttle.batchreq.h0.value(),
                .h1 = p.throttle.batchreq.h1.value(),
                .window = p.throttle.batchreq.window },
        },
        .connection {
            .endpoint = p.endpoint.to_string(), .id = p.id }
    };
}
Token from(const ::api::Token& t)
{
    return {
        .id = t.id.value(),
        .spec = t.spec.to_string(),
        .name = t.name,
        .decimals = t.token_decimals().value(),
    };
}
AssetDetail from(const ::AssetDetail& ad)
{
    return {
        .hash = serialize_hex(ad.hash),
        .id = ad.id.value(),
        .name = ad.name.to_string(),
        .decimals = ad.decimals.value(),
        .height = from(ad.height),
        .ownerAccountId = ad.ownerAccountId.value(),
        .totalSupply = from(::FundsDecimal(ad.totalSupply, ad.decimals)),
        .groupId = ad.group_id.value(),
        .parentId = from(ad.parent_id),
    };
}
AssetLookupTrace from(const ::api::AssetLookupTrace& a)
{
    return {
        .fails = from(a.fails),
        .snapshotHeight = from(a.snapshotHeight)
    };
}
TokenBalanceLookup from(const ::api::TokenBalanceLookup& l)
{
    TokenBalanceLookup out {
        .balance = from(l.balance),
        .token = from(l.token),
        .lookupTrace = from(l.lookupTrace),
        .account = from(l.account),
    };
    return out;
}
TransactionDetails from(const api::TransactionDetails& d)
{
    wrt::Overload convert_transaction(
        [&](const block::Reward& t) -> reward::Transaction {
            return from(t);
        },
        [&](const block::WartTransfer& t) -> wart_transfer::Transaction {
            return from(t);
        },
        [&](const block::TokenTransfer& t) -> token_transfer::Transaction {
            return from(t);
        },
        [&](const block::AssetCreation& t) -> asset_creation::TransactionMaybeProcessed {
            asset_creation::TransactionMaybeProcessed out {
                .data {
                    .name = t.data.name.to_string(),
                    .supply = from(t.data.supply) },
                .processed {},
                .hash { serialize_hex(t.hash) },
                .signedCommon { from(t.signedData) }
            };

            auto& aid { t.data.assetId };
            if (aid) {
                out.processed = asset_creation::Processed { from(*aid) };
            }
            return out;
        },
        [&](const block::NewOrder& t) -> new_order::TransactionMaybeProcessed {
            auto& d { t.data };
            new_order::TransactionMaybeProcessed out {
                .data {
                    .baseAsset { from(d.assetInfo) },
                    .amount { from(d.amount_decimal()) },
                    .limit { make_price(d.limit, d.assetInfo.decimals) },
                    .buy = d.buy },
                .processed {},
                .hash { serialize_hex(t.hash) },
                .signedCommon { from(t.signedData) }
            };
            if (d.remaining) {
                out.processed = new_order::Processed {
                    .remaining = from(::FundsDecimal(*d.remaining, d.assetInfo.decimals))
                };
            }
            return out;
        },
        [&](const block::Match& t) -> match::Transaction {
            return from(t);
        },
        [&](const block::LiquidityDeposit& t) -> liquidity_deposit::TransactionMaybeProcessed {
            auto& d { t.data };
            defi::BaseQuote bq { d.baseDeposited, d.quoteDeposited };
            liquidity_deposit::TransactionMaybeProcessed out {
                .data {
                    .baseAsset { from(d.assetInfo) },
                    .deposited { make_base_quote(bq, d.assetInfo.decimals) } },
                .processed {},
                .hash { serialize_hex(t.hash) },
                .signedCommon { from(t.signedData) }
            };
            if (d.sharesReceived) {
                out.processed = liquidity_deposit::Processed { .sharesReceived = from(
                                                                   ::FundsDecimal(*d.sharesReceived, d.assetInfo.decimals)) };
            }
            return out;
        },
        [&](const block::LiquidityWithdrawal& t) -> liquidity_withdrawal::TransactionMaybeProcessed {
            auto& d { t.data };
            ::FundsDecimal fd(d.sharesRedeemed, TokenDecimals::LIQUIDITY);
            liquidity_withdrawal::TransactionMaybeProcessed out {
                .data {
                    .baseAsset { from(d.assetInfo) },
                    .sharesRedeemed { from(fd) } },
                .processed {},
                .hash { serialize_hex(t.hash) },
                .signedCommon { from(t.signedData) }
            };
            if (d.received) {
                out.processed = liquidity_withdrawal::Processed(make_base_quote(*d.received, d.assetInfo.decimals));
            }
            return out;
        },
        [&](const block::Cancelation& t) -> cancelation::TransactionMaybeProcessed {
            auto& d { t.data };
            // ::FundsDecimal fd(d.sharesRedeemed, TokenDecimals::LIQUIDITY);
            cancelation::TransactionMaybeProcessed out {
                .data { .cancelTxid = from(t.data.cancelTxid) },
                .processed {},
                .hash { serialize_hex(t.hash) },
                .signedCommon { from(t.signedData) }
            };
            if (d.canceledTxHash) {
                out.processed = cancelation::Processed {
                    .canceledTxHash = serialize_hex(*d.canceledTxHash),
                };
            }
            return out;
        });
    auto mined_transaction_details { [&](const TransactionMinedData* md, uint32_t confirmations, auto&& arg) -> TransactionDetails {
        if (md) {
            auto& m { *md };
            return TransactionDetails {
                .transaction { convert_transaction(std::forward<decltype(arg)>(arg).transaction) },
                .type { arg.transaction.data.label },
                .mined { TransactionDetails::Mined {
                    .historyId = from(m.hid),
                    .block {
                        .hegiht = m.block.height.value(),
                        .hash = serialize_hex(m.block.hash),
                        .timestamp = m.block.timestamp,
                    },
                } },
                .confirmations = confirmations,
            };
        } else {
            return TransactionDetails {
                .transaction { convert_transaction(std::forward<decltype(arg)>(arg).transaction) },
                .type { arg.transaction.data.label },
                .mined {},
                .confirmations = confirmations,
            };
        }
    } };
    wrt::Overload convert([&]<typename T>(const api::MaybeMined<T>& a) {
        if (a.mined) 
            return mined_transaction_details(&*a.mined,a.confirmations,a);
        else 
            return mined_transaction_details(nullptr,a.confirmations,a); },
        [&]<typename T>(const api::Mined<T>& a) {
            return mined_transaction_details(&a.mined, a.confirmations, a);
        });
    return d.visit(convert);
}
CompactFee from(::CompactUInt f)
{
    auto uc { f.uncompact() };
    return {
        .str { uc.to_string() },
        .E8 = uc.E8(),
        .bytes { serialize_hex(f.value()) },
    };
}
TransactionMinfeeResult from(const api::TransactionMinfee& f)
{
    return { .minFee = from(f.minfee) };
}

reward::Transaction from(const block::Reward& t)
{
    return {
        .data {
            .toAddress { from(t.data.toAddress) },
            .amount { from(t.data.amount) } },
        .hash { serialize_hex(t.hash) }
    };
}
wart_transfer::Transaction from(const block::WartTransfer& t)
{
    return {
        .data {
            .toAddress = from(t.data.toAddress),
            .amount = from(t.data.amount),
        },
        .hash { serialize_hex(t.hash) },
        .signedCommon = from(t.signedData),
    };
}
token_transfer::Transaction from(const block::TokenTransfer& t)
{
    auto& data { t.data };
    auto funds { ::FundsDecimal(data.amount, data.assetInfo.decimals) };
    return {
        .data {
            .toAddress { data.toAddress.to_string() },
            .amount = from(funds),
            .asset = from(data.assetInfo),
            .isLiquidity = data.isLiquidity,
            .tokenSpec = TokenSpec(data.assetInfo.hash, data.isLiquidity).to_string(),
        },
        .hash { serialize_hex(t.hash) },
        .signedCommon { from(t.signedData) }
    };
}

match::Transaction from(const block::Match& t)
{
    auto& d { t.data };
    std::vector<match::Data::SwapEntry> buySwaps, sellSwaps;
    for (auto& s : t.data.buySwaps) {
        defi::BaseQuote bq { s.base(), s.quote() };
        buySwaps.push_back(
            { .swapped = make_base_quote(bq, d.assetInfo.decimals),
                .historyId = from(s.referred_history_id()) });
    }
    for (auto& s : t.data.sellSwaps) {
        defi::BaseQuote bq { s.base(), s.quote() };
        sellSwaps.push_back(
            { .swapped = make_base_quote(bq, d.assetInfo.decimals),
                .historyId = from(s.referred_history_id()) });
    }
    // std::vector<SwapEntry> sellSwaps;
    return {
        .data {
            .baseAsset { from(d.assetInfo) },
            .poolBefore { make_base_quote(d.poolBefore, d.assetInfo.decimals) },
            .poolAfter { make_base_quote(d.poolBefore, d.assetInfo.decimals) },
            .buySwaps { std::move(buySwaps) },
            .sellSwaps { std::move(sellSwaps) } },

        .hash { serialize_hex(t.hash) },
    };
}

BlockActions from(const api::block::Actions& actions)
{
    BlockActions a;
    a.reward = from(actions.reward);
    a.wartTransfers = from(actions.wartTransfers);
    a.tokenTransfers = from(actions.tokenTransfers);
    for (auto& e : actions.assetCreations) {
        auto& t = e.transaction;
        auto& aid { t.data.assetId };
        assert(aid);
        a.assetCreations.push_back(
            { .transaction = asset_creation::TransactionProcessed {
                  .data {
                      .name = t.data.name.to_string(),
                      .supply = from(t.data.supply) },
                  .processed { .assetId = from(*aid) },
                  .hash { serialize_hex(t.hash) },
                  .signedCommon { from(t.signedData) } },
                .historyId = e.historyId.value() });
    }
    for (auto& e : actions.newOrders) {
        auto& t = e.transaction;
        auto& d { t.data };
        assert(d.remaining);
        a.newOrders.push_back(
            { .transaction = new_order::TransactionProcessed {
                  .data {
                      .baseAsset { from(d.assetInfo) },
                      .amount { from(d.amount_decimal()) },
                      .limit { make_price(d.limit, d.assetInfo.decimals) },
                      .buy = d.buy },
                  .processed {
                      .remaining = from(::FundsDecimal(*d.remaining, d.assetInfo.decimals)) },
                  .hash { serialize_hex(t.hash) },
                  .signedCommon { from(t.signedData) } },
                .historyId = e.historyId.value() });
    }
    a.matches = from(actions.matches);
    for (auto& e : actions.liquidityDeposits) {
        auto& t = e.transaction;
        auto& d { t.data };
        assert(d.sharesReceived);
        defi::BaseQuote bq { d.baseDeposited, d.quoteDeposited };
        a.liquidityDeposits.push_back(
            { .transaction {
                  .data {
                      .baseAsset { from(d.assetInfo) },
                      .deposited { make_base_quote(bq, d.assetInfo.decimals) } },
                  .processed { .sharesReceived = from(::FundsDecimal(*d.sharesReceived, d.assetInfo.decimals)) },
                  .hash { serialize_hex(t.hash) },
                  .signedCommon { from(t.signedData) } },
                .historyId = e.historyId.value() });
    }
    for (auto& e : actions.liquidityWithdrawals) {
        auto& t = e.transaction;
        auto& d { t.data };
        assert(d.received);
        ::FundsDecimal fd(d.sharesRedeemed, TokenDecimals::LIQUIDITY);
        a.liquidityWithdrawals.push_back(
            { .transaction = {
                  .data {
                      .baseAsset { from(d.assetInfo) },
                      .sharesRedeemed { from(fd) } },
                  .processed { .received = make_base_quote(*d.received, d.assetInfo.decimals) },
                  .hash { serialize_hex(t.hash) },
                  .signedCommon { from(t.signedData) } },
                .historyId = e.historyId.value() });
    }
    for (auto& e : actions.cancelations) {
        auto& t = e.transaction;
        auto& d { t.data };
        assert(d.canceledTxHash);
        a.cancelations.push_back(
            { .transaction = {
                  .data { .cancelTxid = from(t.data.cancelTxid) },
                  .processed { .canceledTxHash = serialize_hex(*d.canceledTxHash) },
                  .hash { serialize_hex(t.hash) },
                  .signedCommon { from(t.signedData) } },
                .historyId = e.historyId.value() });
    }
    return a;
}

ActionsByBlock from(const api::TransactionsByBlocks& f)
{
    ActionsByBlock out {
        .perBlock {},
        .fromId = from(f.fromId),
    };

    for (auto& b : std::ranges::reverse_view(f.blocks_reversed)) {
        out.perBlock.push_back(ActionsByBlock::BlockEntry {
            .height = b.height.value(),
            .confirmations = b.confirmations,
            .actions = from(b.actions),
        });
    }
    return out;
}

TransmissionChartsResult::Element from(const rxtx::RangeAggregated& ra)
{
    return {
        .begin = ra.begin.value(),
        .end = ra.end.value(),
        .rx = ra.rx,
        .tx = ra.tx
    };
}
TransmissionChartsResult from(const api::TransmissionTimeseries& ts)
{
    TransmissionChartsResult out;
    for (auto& [host, vec] : ts.byHost) {
        out.byHost.try_emplace(host, from(vec));
    }
    return out;
}
Wallet from(const api::Wallet& w)
{
    auto pubkey { w.pk.pubkey() };
    return {
        .privKey = w.pk.to_string(),
        .pubKey = pubkey.to_string(),
        .address = pubkey.address().to_string()
    };
}
FundsBalance from(const api::FundsBalance& w)
{
    return {
        .total = from(w.total),
        .locked = from(w.locked),
        .mempool = from(w.mempool)
    };
}

WartBalanceResult from(const api::WartBalanceLookup& w)
{
    return {
        .wart = from(w.balance),
        .account = from(w.account)
    };
}

RoundedFeeResult from(const api::Round16Bit& r)
{
    return {
        .original = from(r.original),
        .rounded = from(CompactUInt::compact(r.original))
    };
}
RollbackResult from(const api::Rollback& rb)
{
    return { .length = from(rb.length) };
}
std::vector<std::pair<std::string, size_t>> from(const api::IPCounter& c)
{
    std::vector<std::pair<std::string, size_t>> out;
    for (auto& e : c.vector)
        out.push_back({ e.first.to_string(), e.second });
    return out;
}
NodeInfoResult from(const api::NodeInfo& info)
{
    using namespace std;
    using namespace std::chrono;
    using namespace std::string_literals;
    using sc = std::chrono::steady_clock;
    auto format_duration = [](size_t s /* uptime seconds*/) {
        size_t days = s / (24 * 60 * 60);
        size_t hours = (s % (24 * 60 * 60)) / (60 * 60);
        size_t minutes = (s % (60 * 60)) / 60;
        size_t seconds = (s % 60);
        return to_string(days) + "d "s + to_string(hours) + "h "s + to_string(minutes) + "m "s + to_string(seconds) + "s"s;
    };

    auto startedAt { config().started_at() };
    auto uptime { sc::now() - startedAt.steady };
    size_t uptimeSeconds(duration_cast<seconds>(uptime).count());
    auto uptimeStr { format_duration(uptimeSeconds) };
    uint32_t sinceTimestamp(duration_cast<seconds>(startedAt.system.time_since_epoch()).count());
    return {
        .dbSize = info.dbSize,
        .chainDBPath = config().data.chaindb,
        .peersDBPath = config().data.peersdb,
        .rxtxDBPath = config().data.rxtxdb,
        .version = {
            .name = CMDLINE_PARSER_VERSION,
            .major = VERSION_MAJOR,
            .minor = VERSION_MINOR,
            .patch = VERSION_PATCH,
            .commit = GIT_COMMIT_INFO },
        .uptime = { .since = make_timepoint(sinceTimestamp), .seconds = uint32_t(uptimeSeconds), .formatted = uptimeStr }
    };
}
Candle from(const api::Candle& c)
{
    return {
        c.timestamp.value(),
        c.height.value(),
        c.open,
        c.high,
        c.low,
        c.close,
        c.base,
        c.quote,
    };
}
Trade from(const api::Trade& t)
{
    return {
        t.height.value(),
        t.timestamp.value(),
        t.base,
        t.quote,
    };
}
Error from(::Error e)
{
    return { .code = e.code, .error = e.strerror() };
}

MempoolUpdateResult from(const api::MempoolUpdate& u)
{
    return {
        .deleted = u.deletedTransactions
    };
}

ChainHead from(const api::ChainHead& h)
{
    return {
        .hash = serialize_hex(h.hash),
        .height = h.height.value(),
        .difficulty = h.nextTarget.difficulty(),
        .is_janushash = h.nextTarget.is_janushash(),
        .pinHeight = h.pinHeight.value(),
        .worksum = h.worksum.getdouble(),
        .worksumHex = h.worksum.to_string(),
        .pinHash = serialize_hex(h.pinHash),
        .hashrate = h.hashrate,
    };
}

BlockHeader make_header(const ::Header& header, NonzeroHeight height)
{
    auto version { header.version() };
    const bool testnet { is_testnet() };
    auto powVersion { POWVersion::from_params(height, version, testnet) };
    assert(powVersion.has_value());
    bool verusV2_2 { powVersion->uses_verus_2_2() };
    auto verusHash { verusV2_2 ? verus_hash_v2_2(header) : verus_hash_v2_1(header) };
    auto blockHash { header.hash() };
    auto sha256tHash { hashSHA256(blockHash) };
    auto target { header.target(height, testnet) };
    uint32_t targetBE = hton32(target.binary());
    return {
        .raw = serialize_hex(header),
        .time = make_timepoint(header.timestamp()),
        .target = serialize_hex(targetBE),
        .hash = serialize_hex(header.hash()),
        .pow = {
            .verusV2_2 = verusV2_2,
            .hashVerus = serialize_hex(verusHash),
            .hashSha256t = serialize_hex(sha256tHash),
            .floatVerus = CustomFloat(verusHash).to_double(),
            .floatSha256t = CustomFloat(sha256tHash).to_double(),
        },
        .merkleroot = serialize_hex(header.merkleroot()),
        .nonce = serialize_hex(header.nonce()),
        .prevHash = serialize_hex(header.prevhash()),
        .version = serialize_hex(version.value()),
    };
}
HeaderResult from(const ::api::HeaderInfo& h)
{
    return {
        .header = make_header(h.header, h.height),
        .height = h.height.value(),
    };
}
ChainHeadSynced from(const api::Head& h)
{
    return {
        .chainHead = from(h.chainHead),
        .synced = h.synced
    };
}
// MempoolEntry from(api::MempoolEntry e)
// {
//     std::string hash { serialize_hex(e.txHash) };
//     TransactionSignedCommon signedCommon {
//         .originId = e.from_id().value(),
//         .originAddress = e.from_address(e.txHash).to_string(),
//         .fee = from(e.fee()),
//         .nonceId = e.nonce_id().value(),
//         .pinHeight = e.pin_height().value()
//     };
//     e.visit_overload(
//         [&](const WartTransferMessage& m) -> MempoolEntry {
//             return {
//                 .transaction = wart_transfer::Transaction{
//                     .data = {
//                         .toAddress = m.to_addr().to_string(),
//                         .amount = from(m.wart())
//                     },
//                     .hash=std::move(hash),
//                     .signedCommon{std::move(signedCommon)}
//                 },
//                 .tag = m.label.to_string()
//             }
//         },
//         [&](const TokenTransferMessage& m) {
//             return {
//                 .transaction = token_transfer::Transaction{
//                     .data = {
//                         // .toAddress = m.to_addr().to_string(),
//                         // .amount = from(m.amount())
//
//                     },
//                     .hash=std::move(hash),
//                     .signedCommon{std::move(signedCommon)}
//                 },
//                 .tag = m.label.to_string()
//             }
//             elem["type"] = api::block::TokenTransferData::label;
//             elem["toAddress"] = m.to_addr().to_string();
//             elem["amountU64"] = m.amount().value();
//             elem["assetHash"] = m.asset_hash().hex_string();
//             elem["isLiquidity"] = m.is_liquidity();
//             elem["tokenSpec"] = api::TokenSpec(m.asset_hash(), m.is_liquidity()).to_string();
//         },
//         [&](const LimitSwapMessage& m) {
//             elem["type"] = api::block::NewOrderData::label;
//             elem["assetHash"] = m.asset_hash().hex_string();
//             elem["buy"] = m.buy();
//             elem["amountRaw"] = m.amount().value();
//             elem["limitRaw"] = m.limit().to_double_raw();
//         },
//         [&](const CancelationMessage& m) {
//             elem["type"] = api::block::CancelationData::label;
//             elem["cancelHeight"] = m.cancel_height().value();
//             elem["cancelNonce"] = m.cancel_nonceid();
//         },
//         [&](const LiquidityDepositMessage& m) {
//             elem["type"] = api::block::LiquidityDepositData::label;
//             elem["assetHash"] = m.asset_hash().hex_string();
//             elem["quoteWart"] = m.quote();
//             elem["baseU64"] = m.base().value(); // TODO: this should be looked up and the mempool should only contain elements where it can be looked up (i.e. such elements where the base currency exists)
//         },
//         [&](const LiquidityWithdrawalMessage& m) {
//             elem["type"] = api::block::LiquidityWithdrawalData::label;
//             elem["assetHash"] = m.asset_hash().hex_string();
//             elem["liquidityU64"] = m.amount().value();
//         },
//         [&](const AssetCreationMessage& m) {
//             elem["type"] = api::block::AssetCreationData::label;
//             elem["supply"] = m.supply().to_string();
//             elem["assetName"] = m.asset_name().to_string();
//         });
// }

MempoolEntries from(const ::api::MempoolEntries& e)
{
    MempoolEntries out;
    for (auto& i : e.entries) {
        out.push_back(from(i));
    }
    return out;
}

BlockBinaryAnnotation from(const ParseAnnotation& a)
{
    return {
        .tag = a.tag,
        .beginOffset = a.offsetBegin,
        .endOffset = a.offsetEnd,
        .children = from(a.children)
    };
}

BlockBinaryResult from(const api::BlockBinary& e)
{
    return {
        .bytes { serialize_hex(e.data) },
        .structure = from(e.annotations)
    };
}
std::string from(const TCPPeeraddr& a)
{
    return a.to_string();
}
SignedSnapshot from(const ::SignedSnapshot& ss)
{
    return {
        .hash = serialize_hex(ss.hash),
        .signature = ss.signature.to_string(),
        .priorityHeight = ss.priority.height.value(),
        .priorityImportance = ss.priority.importance
    };
}
Block from(const api::Block& b)
{
    return {
        .header = make_header(b.header, b.height),
        .body = from(b.actions),
        .confirmations = b.confirmations,
        .height = from(b.height)
    };
}
MiningState from(const api::MiningState& ms)
{
    auto& mt { ms.miningTask };
    auto height { mt.block.height };
    auto blockReward { mt.block.body.reward };
    return {
        .synced = ms.synced,
        .header = serialize_hex(mt.block.header),
        .difficulty = mt.block.header.target(height, is_testnet()).difficulty(),
        .merklePrefix = serialize_hex(mt.block.body.merkleLeaves.merkle_prefix()),
        .body = serialize_hex(mt.block.body.data),
        .blockReward = from(blockReward.wart()),
        .height = height.value(),
        .testnet = is_testnet(),
    };
}
std::vector<TransactionId> from(const chainserver::TransactionIds& txids)
{
    std::vector<TransactionId> out;
    for (auto& id : txids)
        out.push_back(from(id));
    return out;
}
HashrateInfo from(const api::HashrateInfo& e)
{
    return {
        .nBlocks = e.nBlocks,
        .estimate = e.estimate,
    };
}
AssetSearchResult from(const api::AssetSearchResult& e)
{
    return {
        .matches = from(e.entries),
        .hashPrefix = e.args.hashPrefix,
        .namePrefix = e.args.namePrefix
    };
}

SwapOrder make_swap_order(const api::Order& o, const TokenDecimals& dec)
{
    return {
        .inMempool = o.confirmations == 0,
        .txHash = serialize_hex(o.txHash),
        .limit = make_price(o.limit, dec),
        .amount = from(::FundsDecimal(o.amount, dec)),
        .filled = from(::FundsDecimal(o.filled, dec)),
    };
}
std::vector<SwapOrder> make_swap_orders(
    const std::vector<api::Order>& orders, const TokenDecimals& dec)
{
    std::vector<SwapOrder> out;
    for (auto& o : orders) {
        out.push_back(make_swap_order(o, dec));
    }
    return out;
}
LiquidityPool make_pool(const api::LiquidityPool& lp, const TokenDecimals dec)
{
    return {
        .asset = from(::FundsDecimal(lp.base, dec)),
        .wart = from(lp.quote),
        .shares = from(::FundsDecimal(lp.shares, TokenDecimals::LIQUIDITY))
    };
}
MarketDetail from(const api::MarketDetail& e)
{
    auto dec { e.base.decimals };
    ToPool toPool {
        .isQuote = false,
        .amount { from(::FundsDecimal(0, dec)) },
    };
    if (auto& tp { e.matchResult.toPool }) {
        bool isQuote = tp->is_quote();
        toPool.isQuote = isQuote;
        if (isQuote) {
            toPool.amount = from(::FundsDecimal(tp->amount(), TokenDecimals::WART));
        } else {
            toPool.amount = from(::FundsDecimal(tp->amount(), dec));
        }
    }
    return {
        .baseAsset = from(e.base),
        .wartToAssetSwaps = make_swap_orders(e.buys, dec),
        .assetToWartSwaps = make_swap_orders(e.sells, dec),
        .liquidityPool = make_pool(e.liquidityPool, dec),
        .match {
            .filled {
                .base = from(::FundsDecimal(e.matchResult.filled.base, dec)),
                .quote = from(::Wart(e.matchResult.filled.quote.value())) },
            .toPool { std::move(toPool) } }
    };
}
MarketOrders from(const api::MarketOrders& e)
{
    auto dec { e.base.decimals };
    return {
        .baseAsset = from(e.base),
        .wartToAssetSwaps = make_swap_orders(e.buys, dec),
        .assetToWartSwaps = make_swap_orders(e.sells, dec),
    };
}
AccountHistory from(const api::AccountHistory& ah)
{
    std::vector<Block> blocks;
    for (auto& b : std::ranges::reverse_view(ah.blocks_reversed)) {
        blocks.push_back(from(b));
    }
    return {
        .fromId = ah.fromId.value(),
        .perBlock = std::move(blocks)
    };
}

RichlistResult from(const api::RichlistInfo& ri)
{
    std::vector<RichlistEntry> entries;
    for (auto& [address, funds] : ri.richlist.entries) {
        entries.push_back({ .address = address.to_string(),
            .balance = from(::FundsDecimal(funds, ri.token.token_decimals()))

        });
    }
    return {
        .token = from(ri.token),
        .richlist = std::move(entries),
    };
}
OffenseEntry from(const ::OffenseEntry& e)
{
    return {
        .ip = e.ip.to_string(),
        .time = make_timepoint(e.timestamp),
        .offense = e.offense.err_name()
    };
}
std::string from(const IP& ip)
{
    return ip.to_string();
}
ThrottleState from(const api::ThrottleState& s)
{
    using namespace std::chrono;

    return {
        .delay = int(duration_cast<seconds>(s.delay).count()),
        .blockRequest {
            .h0 = s.blockreq.h0.value(),
            .h1 = s.blockreq.h1.value(),
            .window = s.blockreq.window },
        .headerRequest {
            .h0 = s.batchreq.h0.value(),
            .h1 = s.batchreq.h1.value(),
            .window = s.batchreq.window }
    };
}

Peerinfo from(const api::Peerinfo& pi)
{
    auto& pgrid { pi.chainstate.descripted()->grid() };
    std::vector<std::string> grid;
    grid.reserve(pgrid.size());
    for (HeaderView hv : pgrid) {
        grid.push_back(serialize_hex(hv));
    }
    return {
        .connection = {
            .since = make_timepoint(pi.since),
            .port = pi.endpoint.port(),
            .ip = from(pi.endpoint.ip()),
        },
        .throttle = from(pi.throttle),
        .chain = { // uint32_t length;
            .length = pi.chainstate.descripted()->chain_length().value(),
            .forkLower = pi.chainstate.consensus_fork_range().lower().value(),
            .forkUpper = pi.chainstate.consensus_fork_range().upper().value(),
            .descriptor = pi.chainstate.descripted()->descriptor.value(),
            .worksum = pi.chainstate.descripted()->worksum().getdouble(),
            .worksumHex = pi.chainstate.descripted()->worksum().to_string(),
            .grid = std::move(grid) },
        .leaderPriority = { .ack { .importance = pi.acknowledgedSnapshotPriority.importance, .hegiht = pi.acknowledgedSnapshotPriority.height.value() }, .theirs { .importance = pi.theirSnapshotPriority.importance, .hegiht = pi.theirSnapshotPriority.height.value() } }
    };
}
HashrateBlockChart from(const api::HashrateBlockChart& chart)
{
    return {
        .range {
            .begin = chart.range.begin.value(),
            .end = chart.range.end.value() },
        .data = std::move(chart.chart)
    };
}

HashrateTimeChart from(const api::HashrateTimeChart& chart)
{
    std::vector<HashrateTimeChart::Entry> data;
    for (auto& [timestamp, height, hashrate] : std::ranges::reverse_view(chart.chartReversed)) {
        data.push_back({ .timestamp = timestamp,
            .height = height.value(),
            .hashrate = hashrate });
    }
    return {
        .range {
            .begin = chart.begin,
            .end = chart.end },
        .data = std::move(data),
        .interval = chart.interval,
    };
}
ParsedPrice from(const api::ParsedPrice& e)
{
    return {
        .decimals = e.dec.value(),
        .floor = make_price(e.floor, e.dec),
        .ceil = make_price(e.ceil, e.dec)
    };
}
double from(const api::JanushashNumber& e)
{
    return e.d;
}
std::vector<PeerinfoConnection> from(const api::PeerinfoConnections& pcs){
    std::vector<PeerinfoConnection> out;
    for (auto &pi : pcs.v) {
        out.push_back(
        {
            .since = make_timepoint(pi.since),
            .port = pi.endpoint.port(),
            .ip = from(pi.endpoint.ip()),
        });
    }
    return out;
}
}
}

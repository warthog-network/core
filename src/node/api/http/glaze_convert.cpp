#include "glaze_convert.hpp"
#include "api/types/all.hpp"
#include "crypto/hash.hpp"
#include "general/funds.hpp"
#include "general/hex.hpp"
#include "transport/helpers/peer_addr.hpp"
#include <ranges>

namespace api {
namespace glaze {

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
static Price make_price(Price_uint64 p, TokenDecimals d)
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
    return WithHistoryId { .data = from(tx.transaction), .historyId = tx.historyId.value() };
}

template <typename T>
static auto from(const wrt::optional<T>& o)
{
    std::optional<target_type<T>> out;
    if (o)
        out = from(*o);
    return out;
}

template <typename T>
static auto from(const std::vector<T>& v)
{
    std::vector<target_type<T>> out;
    for (auto& e : v) {
        out.push_back(from(e));
    }
    return out;
}

namespace {

FundsDecimal from(::FundsDecimal fd)
{
    return {
        .str = fd.to_string(),
        .u64 = fd.funds.value(),
        .decimals = fd.decimals.value()
    };
}
std::string convert(const ::Peeraddr a)
{
    return a.to_string();
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

Wart from(const ::Wart w)
{
    return {
        .str { w.to_string() },
        .E8 = w.E8(),
    };
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
                .h1 = p.throttle.blockreq.h0.value(),
                .h2 = p.throttle.blockreq.h1.value(),
                .window = p.throttle.batchreq.window },
            .headerRequest {
                .h1 = p.throttle.batchreq.h0.value(),
                .h2 = p.throttle.batchreq.h1.value(),
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
TransactionDetails to_json(const api::TransactionDetails& d)
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
            if (d.filled) {
                out.processed = new_order::Processed {
                    .filled = from(::FundsDecimal(*d.filled, d.assetInfo.decimals))
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
        [&](const block::TransactionCancelation& t) -> cancelation::TransactionMaybeProcessed {
            auto& d { t.data };
            // ::FundsDecimal fd(d.sharesRedeemed, TokenDecimals::LIQUIDITY);
            cancelation::TransactionMaybeProcessed out {
                .data { .cancelTxid = from(t.data.cancelTxid) },
                .processed {},
                .hash { serialize_hex(t.hash) },
                .signedCommon { from(t.signedData) }
            };
            if (d.canceledOrder) {
                auto& o { *d.canceledOrder };
                out.processed = cancelation::Processed {
                    .baseAsset = from(o.assetInfo),
                    .buy = o.buy,
                    .historyId = from(o.historyId),
                    .remaining = from(::FundsDecimal(o.remaining, o.assetInfo.decimals))
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
TransactionMinfee from(const api::TransactionMinfee& f)
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
asset_creation::TransactionProcessed from_mined(const block::AssetCreation& t)
{
    assert(t.data.assetId);
    return {
        .data {
            .name { t.data.name.to_string() },
            .supply = from(t.data.supply) },
        .processed = { t.data.assetId->value() },
        .hash { serialize_hex(t.hash) },
        .signedCommon = from(t.signedData),
    };
}

new_order::TransactionProcessed from_mined(const block::NewOrder& t)
{
    assert(t.data.filled);
    return {
        .data {
            .baseAsset = from(t.data.assetInfo),
            .amount = from(::FundsDecimal(t.data.amount, t.data.assetInfo.decimals)),
            .limit = make_price(t.data.limit, t.data.assetInfo.decimals),
            .buy = t.data.buy,
        },
        .processed = { from(::FundsDecimal(*t.data.filled, t.data.assetInfo.decimals)) },
        .hash { serialize_hex(t.hash) },
        .signedCommon = from(t.signedData),
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
// liquidity_deposit::TransactionProcessed from_mined(const block::LiquidityDeposit& t)
// {
// }
// liquidity_withdrawal::TransactionProcessed from_mined(const api::block::WithHistoryId<block::LiquidityWithdrawal>& t)
// {
// }
// cancelation::TransactionProcessed from_mined(const block::TransactionCancelation t)
// {
// }

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
            .actions = {},
        });
        auto& a { out.perBlock.back().actions };
        // TODO
        a.reward = from(b.actions.reward);
        a.wartTransfers = from(b.actions.wartTransfers);
        a.tokenTransfers = from(b.actions.tokenTransfers);
        // a.assetCreations = from(b.actions.assetCreations);
        // a.newOrders = from(b.actions.newOrders);
        // a.matches = from(b.actions.matches);
        // a.liquidityDeposits = from(b.actions.liquidityDeposit);
        // a.liquidityWithdrawals = from(b.actions.liquidityWithdrawal);
        // a.cancelations = from(b.actions.cancelations);
    }
    return out;
}
}
}

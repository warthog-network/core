#include "interface.hpp"
#include "api/types/shared.hpp"
#include "block/header/header_impl.hpp"
#include "chainserver/server.hpp"
#include "communication/rxtx_server/rxtx_server.hpp"
#include "eventloop/eventloop.hpp"
#include "general/logger/log_memory.hpp"
#include "global/globals.hpp"
#include "transport/tcp/conman.hpp"
#include <nlohmann/json.hpp>

// mempool functions

void lookup_tx(const TxHash& hash, TxCb f)
{
    api_call<chainserver::LookupTxHash>(hash, std::move(f));
}

void get_latest_transactions(LatestTxsCb f)
{
    global().chainServer->api_call(chainserver::LatestTxs(), std::move(f));
}

void get_transaction_minfee(TransactionMinfeeCb f)
{
    global().chainServer->api_call(chainserver::GetTransactionMinfee(), std::move(f));
}

// peer functions

void get_ip_count(IpCounterCb&& cb)
{
    global().core->api_count_ips(std::move(cb));
}

void get_banned_peers(PeerServer::banned_callback_t&& f)
{
    global().peerServer->async_get_banned(std::move(f));
}
void unban_peers(ErrorCb&& f)
{
    global().peerServer->async_unban(std::move(f));
}

void get_connection_schedule(JSONCb&& cb)
{
    global().core->api_get_connection_schedule(std::move(cb));
}
void get_offense_entries(ErrorCb&& f)
{
    global().peerServer->async_unban(std::move(f));
}
// void get_connected_peers(Conman::PeersCB f)
// {
//     global().pcm->async_get_peers(f);
// }
void get_connected_peers2(PeersCb&& cb)
{
    global().core->api_get_peers(std::move(cb));
}

void disconnect_peer(uint64_t id, ErrorCb&& cb)
{
    global().core->api_disconnect_peer(id, std::move(cb));
}
void get_throttled_peers(ThrottledCb&& cb)
{
    global().core->api_get_throttled(std::move(cb));
}

void get_connected_connection(ConnectedConnectionCb&& cb)
{
    global().core->api_get_peers([cb = std::move(cb)](const std::vector<api::Peerinfo>& pi) {
        cb({ pi });
    });
}

void set_minfee(uint64_t fee, MempoolConstraintCb cb)
{
    set_config().minMempoolFee = CompactUInt::compact(Wart::from_value_throw(fee), true);
    api_call<chainserver::MempoolConstraintUpdate>(std::move(cb));
}

void get_round16bit_e8(uint64_t e8, RoundCb cb)
{
    cb(api::Round16Bit { Wart::from_value_throw(e8) });
}

void get_round16bit_funds(Wart f, RoundCb cb)
{
    cb(api::Round16Bit { f });
}

void get_info(InfoCb cb)
{
    api_call<chainserver::GetDBSize>(
        [cb = std::move(cb)](Result<api::DBSize> s) {
            if (s)
                cb(api::NodeInfo { std::move(*s) });
            else
                cb(Error(ENOTFOUND));
        });
}

void get_wallet_new(WalletCb cb)
{
    cb(api::Wallet {});
}

void get_wallet_from_privkey(const PrivKey& pk, WalletCb cb)
{
    cb(api::Wallet { pk });
}

void get_janushash_number(std::string_view sv, ResultCb<api::JanushashNumber> cb)
{
    // LATER: do header check in different thread
    Header h;
    if (!HexRef(sv).parse_to(h))
        cb(Error(EBADHEADER));
    cb(api::JanushashNumber { h.janus_number() });
}

void parse_price(std::string_view priceStr, TokenPrecision p, ResultCb<api::ParsedPrice> cb)
{
    if (auto d { try_parse<double>(priceStr) }) {
        auto floor { Price_uint64::from_double_adjusted(*d, p, false) };
        auto ceil { Price_uint64::from_double_adjusted(*d, p, true) };
        if (floor && ceil)
            cb(api::ParsedPrice { p, *floor, *ceil });
    }
    cb(Error(EBADPRICE));
}

void sample_verified_peers(size_t n, SampledPeersCb cb)
{
    global().core->api_sample_verified_peers(n, std::move(cb));
}

namespace {
struct APIHeadRequest {

    APIHeadRequest(HeadCb cb)
        : cb(std::move(cb))
    {
    }
    void on(const Result<api::ChainHead>&& e)
    {
        if (e.has_value()) {
            on(std::move(e.value()));
            try_cb();
        } else {
            std::lock_guard l(m);
            if (sent == false) {
                cb(e.error());
                sent = true;
            }
        }
    }
    void on(bool set_synced)
    {
        synced = set_synced;
        try_cb();
    }

private:
    void on(const api::ChainHead& h)
    {
        head = std::move(h);
    }
    bool all_set() const { return head.has_value() && synced.has_value(); }
    void try_cb()
    {
        std::lock_guard l(m);
        if (sent == false && all_set()) {
            sent = true;
            cb(
                api::Head {
                    .chainHead { std::move(*head) },
                    .synced = this->synced.value() });
        }
    }
    std::mutex m;
    bool sent { false };
    wrt::optional<api::ChainHead> head;
    wrt::optional<bool> synced;
    HeadCb cb;
};
}

// chain functions
void get_block_head(HeadCb f)
{

    auto s = std::make_shared<APIHeadRequest>(std::move(f));

    global().core->api_is_synced([s](auto&& ch) {
        s->on(std::move(ch));
    });
    api_call<chainserver::GetChainHead>([s = std::move(s)](auto&& ch) {
        s->on(std::move(ch));
    });
}

namespace {
struct APIMiningRequest {

    APIMiningRequest(MiningCb cb)
        : cb(std::move(cb))
    {
    }
    void on(const Result<ChainMiningTask>&& e)
    {
        if (e.has_value()) {
            on(std::move(e.value()));
            try_cb();
        } else {
            std::lock_guard l(m);
            if (sent == false) {
                cb(e.error());
                sent = true;
            }
        }
    }
    void on(bool set_synced)
    {
        synced = set_synced;
        try_cb();
    }

private:
    void on(const ChainMiningTask& t)
    {
        miningTask = std::move(t);
    }
    bool all_set() const { return miningTask.has_value() && synced.has_value(); }
    void try_cb()
    {
        std::lock_guard l(m);
        if (sent == false && all_set()) {
            sent = true;
            cb(
                api::MiningState {
                    .miningTask { std::move(*miningTask) },
                    .synced = this->synced.value() });
        }
    }
    std::mutex m;
    bool sent { false };
    wrt::optional<ChainMiningTask> miningTask;
    wrt::optional<bool> synced;
    MiningCb cb;
};
}

void get_chain_mine(const Address& a, MiningCb f)
{
    auto s = std::make_shared<APIMiningRequest>(std::move(f));

    global().core->api_is_synced([s](auto&& ch) {
        s->on(std::move(ch));
    });
    api_call<chainserver::GetMining>(a,
        [s = std::move(s)](auto&& ch) {
            s->on(std::move(ch));
        });
}

mining_subscription::MiningSubscription subscribe_chain_mine(Address address, mining_subscription::callback_t callback)
{
    return global().chainServer->api_subscribe_mining(address, std::move(callback));
}

void get_hashrate_n(size_t n, HashrateCb&& cb)
{
    global().core->api_get_hashrate(std::move(cb), n);
}
void get_hashrate(HashrateCb&& cb)
{
    global().core->api_get_hashrate(std::move(cb));
}
void get_hashrate_block_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateBlockChartCb&& cb)
{
    global().core->api_get_hashrate_block_chart(from, to, window, std::move(cb));
}

void get_hashrate_time_chart(uint32_t from, uint32_t to, size_t window, HashrateTimeChartCb&& cb)
{
    global().core->api_get_hashrate_time_chart(from, to, window, std::move(cb));
}

void put_chain_append(BlockWorker&& bw, ErrorCb f)
{
    global().chainServer->api_call(chainserver::MiningAppend(bw.block, bw.worker), f);
}
void get_signed_snapshot(Eventloop::SignedSnapshotCb&& cb)
{
    global().core->defer(std::move(cb));
}

// account functions
void get_account_token_balance(const api::AccountIdOrAddress& address, const api::TokenIdOrSpec& t, TokenBalanceCb cb)
{
    global().chainServer->api_call(chainserver::GetTokenBalance { address, t }, cb);
}

void get_token_richlist(const api::TokenIdOrSpec& token, RichlistCb f)
{
    global().chainServer->api_call(chainserver::GetRichlist { token }, std::move(f));
}
void get_transmission_minutes(TransmissionCb cb)
{
    using namespace std::chrono;
    auto begin { duration_cast<seconds>((system_clock::now() - days(1)).time_since_epoch()).count() };
    global().rxtxServer->api_get_aggregate_minutes({ .cb { std::move(cb) },
        .range { begin, std::numeric_limits<uint32_t>::max() } });
}
void get_transmission_hours(TransmissionCb cb)
{
    using namespace std::chrono;
    auto begin { duration_cast<seconds>((system_clock::now() - days(10)).time_since_epoch()).count() };
    global().rxtxServer->api_get_aggregate_hours({ .cb { std::move(cb) },
        .range { begin, std::numeric_limits<uint32_t>::max() } });
}

void inspect_eventloop(std::function<void(const Eventloop& e)>&& cb)
{
    global().core->api_inspect(std::move(cb));
}

void subscribe_chain_event(SubscriptionRequest r)
{
    global().chainServer->subscribe_chain_event(std::move(r));
}
void subscribe_connection_event(SubscriptionRequest r)
{
    global().core->subscribe_connection_event(std::move(r));
}
void subscribe_account_event(SubscriptionRequest r, Address a)
{
    global().chainServer->subscribe_account_event(std::move(r), std::move(a));
}
void subscribe_minerdist_event(SubscriptionRequest r)
{
    global().chainServer->subscribe_minerdist_event(std::move(r));
}
void subscribe_log_event(SubscriptionRequest r)
{
    logging::logMemory.subscribe(std::move(r));
}

void destroy_all_subscriptions(subscription_data_ptr p)
{
    global().chainServer->destroy_subscriptions(p);
    global().core->destroy_subscriptions(p);
}
void loadtest_block(uint64_t conId, ErrorCb cb)
{
    global().core->api_loadtest_block(conId, std::move(cb));
}
void loadtest_header(uint64_t conId, ErrorCb cb)
{
    global().core->api_loadtest_header(conId, std::move(cb));
}
void loadtest_disable(uint64_t conId, ErrorCb cb)
{
    global().core->api_loadtest_disable(conId, std::move(cb));
}

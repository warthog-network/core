#include "interface.hpp"
#include "api/types/all.hpp"
#include "asyncio/conman.hpp"
#include "block/header/header_impl.hpp"
#include "chainserver/server.hpp"
#include "eventloop/eventloop.hpp"
#include "global/globals.hpp"

// mempool functions
void put_mempool(PaymentCreateMessage&& m, MempoolInsertCb cb)
{
    global().pcs->api_put_mempool(std::move(m), std::move(cb));
}

void get_mempool(MempoolCb cb)
{
    global().pcs->api_get_mempool(std::move(cb));
}

void lookup_tx(const API::HistoryIdOrHash h, TxCb f)
{
    global().pcs->api_lookup_tx(h, std::move(f));
}

void get_latest_transactions(LatestTxsCb f)
{
    global().pcs->api_lookup_latest_txs(std::move(f));
}

void get_transaction_minfee(TransactionMinfeeCb f){
    global().pcs->api_get_transaction_minfee(std::move(f));
}

// peer db functions

void get_banned_peers(PeerServer::BannedCB&& f)
{
    global().pps->async_get_banned(std::move(f));
}
void unban_peers(ResultCb&& f)
{
    global().pps->async_unban(std::move(f));
}
void get_offense_entries(ResultCb&& f)
{
    global().pps->async_unban(std::move(f));
}
// void get_connected_peers(Conman::PeersCB f)
// {
//     global().pcm->async_get_peers(f);
// }
void get_connected_peers2(PeersCb&& cb)
{
    global().pel->api_get_peers(std::move(cb));
}

void get_connected_connection(ConnectedConnectionCB&& cb)
{
    global().pel->api_get_peers([cb = std::move(cb)](const std::vector<API::Peerinfo>& pi) {
        cb({ pi });
    });
}

void set_minfee(uint64_t fee, MempoolConstraintCb cb)
{
    set_config().node.minMempoolFee = CompactUInt::compact(Funds::from_value_throw(fee), true);
    global().pcs->async_notify_mempool_constraint_update(cb);
}

void get_round16bit_e8(uint64_t e8, RoundCb cb)
{
    cb(API::Round16Bit { Funds::from_value_throw(e8) });
}

void get_round16bit_funds(Funds f, RoundCb cb)
{
    cb(API::Round16Bit { f });
}
void get_version(VersionCb cb)
{
    cb(PrintNodeVersion {});
}

void get_wallet_new(WalletCb cb)
{
    cb(API::Wallet {});
}

void get_wallet_from_privkey(const PrivKey& pk, WalletCb cb)
{
    cb(API::Wallet { pk });
}

void get_janushash_number(std::string_view sv, RawCb cb)
{
    // TODO: do header check in different thread
    Header h;
    if (!parse_hex(sv, h))
        cb({ "" });

    auto double_to_string = [](double d) {
        std::string s;
        s.resize(35);
        auto n { std::snprintf(s.data(), s.size(), "%.20e", d) };
        s.resize(n);
        return s;
    };
    cb({ double_to_string(h.janus_number()) });
}

namespace {
struct APIHeadRequest {

    APIHeadRequest(HeadCb cb)
        : cb(std::move(cb))
    {
    }
    void on(const tl::expected<API::ChainHead, int32_t>&& e)
    {
        if (e.has_value()) {
            on(std::move(e.value()));
            try_cb();
        } else {
            std::lock_guard l(m);
            if (sent == false) {
                cb(tl::make_unexpected(e.error()));
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
    void on(const API::ChainHead& h)
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
                API::Head {
                    .chainHead { std::move(*head) },
                    .synced = this->synced.value() });
        }
    }
    std::mutex m;
    bool sent { false };
    std::optional<API::ChainHead> head;
    std::optional<bool> synced;
    HeadCb cb;
};
}

// chain functions
void get_block_head(HeadCb f)
{

    auto s = std::make_shared<APIHeadRequest>(std::move(f));

    global().pel->api_get_synced([s](auto&& ch) {
        s->on(std::move(ch));
    });
    global().pcs->async_get_head([s = std::move(s)](auto&& ch) {
        s->on(std::move(ch));
    });
}

namespace {
struct APIMiningRequest {

    APIMiningRequest(MiningCb cb)
        : cb(std::move(cb))
    {
    }
    void on(const tl::expected<ChainMiningTask, Error>&& e)
    {
        if (e.has_value()) {
            on(std::move(e.value()));
            try_cb();
        } else {
            std::lock_guard l(m);
            if (sent == false) {
                cb(tl::make_unexpected(e.error()));
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
                API::MiningState {
                    .miningTask { std::move(*miningTask) },
                    .synced = this->synced.value() });
        }
    }
    std::mutex m;
    bool sent { false };
    std::optional<ChainMiningTask> miningTask;
    std::optional<bool> synced;
    MiningCb cb;
};
}

void get_chain_mine(const Address& a, MiningCb f)
{
    auto s = std::make_shared<APIMiningRequest>(std::move(f));

    global().pel->api_get_synced([s](auto&& ch) {
        s->on(std::move(ch));
    });
    global().pcs->api_get_mining(a,
        [s = std::move(s)](auto&& ch) {
            s->on(std::move(ch));
        });
}

mining_subscription::MiningSubscription subscribe_chain_mine(Address address, mining_subscription::callback_t callback)
{
    return global().pcs->api_subscribe_mining(address, std::move(callback));
}

void get_chain_header(API::HeightOrHash hh, HeaderCb f)
{
    global().pcs->api_get_header(hh, f);
}
void get_chain_hash(Height hh, HashCb f)
{
    global().pcs->api_get_hash(hh, f);
}

void get_chain_grid(GridCb f)
{
    global().pcs->api_get_grid(f);
}
void get_chain_block(API::HeightOrHash hh, BlockCb cb)
{
    global().pcs->api_get_block(hh, cb);
}

void get_txcache(TxcacheCb&& cb)
{
    global().pcs->api_get_txcache(std::move(cb));
}

void get_hashrate_n(size_t n, HashrateCb&& cb)
{
    global().pel->api_get_hashrate(std::move(cb), n);
}
void get_hashrate(HashrateCb&& cb)
{
    global().pel->api_get_hashrate(std::move(cb));
}
void get_hashrate_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateChartCb&& cb)
{
    global().pel->api_get_hashrate_chart(from, to, window, std::move(cb));
}

void put_chain_append(ChainMiningTask&& mt, ResultCb f)
{
    global().pcs->api_mining_append(std::move(mt.block), f);
}
void get_signed_snapshot(Eventloop::SignedSnapshotCb&& cb)
{
    global().pel->defer(std::move(cb));
}

// account functions
void get_account_balance(const API::AccountIdOrAddress& address, BalanceCb f)
{
    global().pcs->api_get_balance(address, f);
}

void get_account_history(const Address& address, uint64_t beforeId,
    HistoryCb f)
{
    global().pcs->api_get_history(address, beforeId, f);
}

void get_account_richlist(RichlistCb f)
{
    global().pcs->api_get_richlist(f);
}

void inspect_conman(std::function<void(const Conman& e)>&& cb)
{
    global().pcm->async_inspect(std::move(cb));
}

void inspect_eventloop(std::function<void(const Eventloop& e)>&& cb)
{
    global().pel->api_inspect(std::move(cb));
}

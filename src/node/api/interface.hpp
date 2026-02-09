#pragma once

#include "callbacks.hpp"
#include "chainserver/mining_subscription.hpp"
#include "chainserver/server.hpp"
#include "eventloop/eventloop.hpp"
#include "events/subscription_fwd.hpp"
#include "global/globals.hpp"
#include "transport/tcp/conman.hpp"

template <typename R, typename T>
struct APICall;

template <IsAPIRequest R, typename... ReqArgs>
struct APICall<R, std::tuple<ReqArgs...>> {
    static void invoke(const ReqArgs&... args, R::Callback cb)
    {
        if constexpr (ChainServer::supports<R>) {
            global().chainServer->api_call(R(args...), std::move(cb));
        } else {
            static_assert(false, "API request type not supported");
        }
    }
};

template <typename R>
inline constexpr auto api_call { &APICall<R, typename R::base_tuple_t>::invoke };

// mempool functions
void lookup_tx(const TxHash& hash, TxCb f);

void get_latest_transactions(LatestTxsCb f);
void get_transaction_minfee(TransactionMinfeeCb f);

// peer functions
void get_ip_count(IpCounterCb&& cb);
void get_banned_peers(PeerServer::banned_callback_t&& cb);
void unban_peers(ErrorCb&& cb);
void get_connection_schedule(JSONCb&& cb);

inline void get_offenses(Page page, PeerServer::offenses_callback_t&& cb)
{
    global().peerServer->async_get_offenses(page, std::move(cb));
}

// void get_connected_peers(Conman::PeersCB cb);

void get_failed_addresses(PeerServer::banned_callback_t cb);
void get_verified_addresses(PeerServer::banned_callback_t cb);

void get_connected_peers2(PeersCb&& cb);
void disconnect_peer(uint64_t, ErrorCb&& cb);
void get_throttled_peers(ThrottledCb&& cb);
void get_connected_connection(ConnectedConnectionCb&& cb);

void set_minfee(uint64_t fee, MempoolConstraintCb cb);

// tools functions
void get_round16bit_e8(uint64_t e8, RoundCb cb);
void get_round16bit_funds(Wart w, RoundCb cb);
void get_info(InfoCb cb);
void get_wallet_new(WalletCb cb);
void get_wallet_from_privkey(const PrivKey& pk, WalletCb cb);
void get_janushash_number(std::string_view, ResultCb<api::JanushashNumber> cb);
void parse_price(std::string_view, TokenPrecision precision, ResultCb<api::ParsedPrice> cb);
void sample_verified_peers(size_t n, SampledPeersCb cb);

// chain functions
void get_block_head(HeadCb cb);
void get_chain_mine(const Address& a, MiningCb cb);
mining_subscription::MiningSubscription subscribe_chain_mine(Address address, mining_subscription::callback_t callback);
void get_hashrate_n(size_t n, HashrateCb&& cb);
void get_hashrate(HashrateCb&& cb);
void get_hashrate_block_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateBlockChartCb&& cb);
void get_hashrate_time_chart(uint32_t from, uint32_t to, size_t window, HashrateTimeChartCb&& cb);
void put_chain_append(BlockWorker&& mt, ErrorCb cb);
void get_signed_snapshot(Eventloop::SignedSnapshotCb&& cb);

// sync functions
void get_headerdownload(HeaderdownloadCb f);

// account functions
void get_account_token_balance(const api::AccountIdOrAddress& address, const api::TokenIdOrSpec&, TokenBalanceCb cb);
void get_token_richlist(const api::TokenIdOrSpec&, RichlistCb cb);

void get_transmission_minutes(TransmissionCb cb);
void get_transmission_hours(TransmissionCb cb);

// endpoints function
void inspect_eventloop(std::function<void(const Eventloop& e)>&&);

void health_state(HealthCb);
void subscribe_chain_event(SubscriptionRequest);
void subscribe_connection_event(SubscriptionRequest);
void subscribe_account_event(SubscriptionRequest, Address);
void subscribe_minerdist_event(SubscriptionRequest);
void subscribe_log_event(SubscriptionRequest);
void destroy_all_subscriptions(subscription_data_ptr);
void loadtest_block(uint64_t conId, ErrorCb);
void loadtest_header(uint64_t conId, ErrorCb);
void loadtest_disable(uint64_t conId, ErrorCb);


#pragma once

#include "asyncio/conman.hpp"
#include "callbacks.hpp"
#include "chainserver/mining_subscription.hpp"
#include "eventloop/eventloop.hpp"
#include "global/globals.hpp"

// mempool cbunctions
void put_mempool(PaymentCreateMessage&&, MempoolInsertCb);
void get_mempool(MempoolCb cb);
void lookup_tx(const API::HistoryIdOrHash hash, TxCb f);

void get_latest_transactions(LatestTxsCb f);
void get_transaction_minfee(TransactionMinfeeCb f);

// peer db functions
void get_banned_peers(PeerServer::BannedCB&& cb);
void unban_peers(ResultCb&& cb);

inline void get_offenses(Page page, PeerServer::OffensesCb&& cb)
{
    global().pps->async_get_offenses(page, std::move(cb));
}

// void get_connected_peers(Conman::PeersCB cb);

void get_failed_addresses(PeerServer::BannedCB cb);
void get_verified_addresses(PeerServer::BannedCB cb);

void get_connected_peers2(PeersCb&& cb);
void get_connected_connection(ConnectedConnectionCB&& cb);

void set_minfee(uint64_t fee, MempoolConstraintCb cb);

// tools functions
void get_round16bit_e8(uint64_t e8, RoundCb cb);
void get_round16bit_funds(Funds e8, RoundCb cb);
void get_version(VersionCb cb);
void get_wallet_new(WalletCb cb);
void get_wallet_from_privkey(const PrivKey& pk, WalletCb cb);
void get_janushash_number(std::string_view, RawCb cb);

// chain functions
void get_block_head(HeadCb cb);
void get_chain_mine(const Address& a, MiningCb cb);
mining_subscription::MiningSubscription subscribe_chain_mine(Address address, mining_subscription::callback_t callback);
void get_chain_header(API::HeightOrHash, HeaderCb cb);
void get_chain_hash(Height height, HashCb cb);
void get_chain_grid(GridCb cb);
void get_chain_block(API::HeightOrHash, BlockCb cb);
void get_txcache(TxcacheCb&& cb);
void get_hashrate_n(size_t n, HashrateCb&& cb);
void get_hashrate(HashrateCb&& cb);
void get_hashrate_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateChartCb&& cb);
void put_chain_append(ChainMiningTask&& mt, ResultCb cb);
void get_signed_snapshot(Eventloop::SignedSnapshotCb&& cb);

// sync functions
void get_headerdownload(HeaderdownloadCb f);

// account functions
void get_account_balance(const API::AccountIdOrAddress& address, BalanceCb cb);
void get_account_history(const Address& address, uint64_t end, HistoryCb cb);
void get_account_richlist(RichlistCb cb);

// endpoints function
void inspect_eventloop(std::function<void(const Eventloop& e)>&&);
void inspect_conman(std::function<void(const Conman& c)>&&);

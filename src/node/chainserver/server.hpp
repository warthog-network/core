#pragma once
#include "api/callbacks.hpp"
#include "api/events/subscription_fwd.hpp"
#include "server_fwd.hpp"
// #include "api/types/height_or_hash.hpp"
#include "api_types.hpp"
#include "chainserver/mining_subscription.hpp"
#include "chainserver/subscription_state.hpp"
#include "general/errors.hpp"
#include "markethistory/server.hpp"
// #include "communication/create_transaction.hpp"
#include "communication/stage_operation/request.hpp"
#include "state/state.hpp"
#include <condition_variable>
#include <thread>

#define LIST_API_TYPES(XX)                                                       \
    XX(MiningAppend, void, Block, block, std::string, worker)                    \
    XX(PutMempool, TxHash, TransactionCreate, message)                           \
    XX(LatestTxs, api::TransactionsByBlocks)                                     \
    XX(LookupTxByHash, api::TransactionDetails, TxHash, hash)                    \
    XX(GetHeader, api::HeaderInfo, api::HeightOrHash, heightOrHash)              \
    XX(GetTransactionMinfee, api::TransactionMinfee)                             \
    XX(GetGrid, Grid)                                                            \
    XX(FakeMine, void, Address, address)                                         \
    XX(FakeMineToZero, void)                                                     \
    XX(Rollback, void)                                                           \
    XX(GetTxcache, chainserver::TransactionIds)                                  \
    XX(GetBlock, api::Block, api::HeightOrHash, heightOrHash)                    \
    XX(GetMining, ChainMiningTask, Address, address)                             \
    XX(GetBlockBinary, api::BlockBinary, api::HeightOrHash, heightOrHash)        \
    XX(MarketDetail, api::MarketDetail, api::AssetIdOrHash, asset)               \
    XX(LookupAsset, api::Asset, api::AssetIdOrHash, asset)                       \
    XX(CompleteAsset, api::AssetSearchResult,                                    \
        std::string, namePrefix, std::string, hashPrefix)                        \
    XX(MempoolConstraintUpdate, api::MempoolUpdate)                              \
    XX(GetDBSize, api::DBSize)                                                   \
    XX(GetChainHead, api::ChainHead)                                             \
    XX(GetWartBalance, api::WartBalanceLookup, api::AccountIdOrAddress, account) \
    XX(GetAccountMempool, api::MempoolEntries, api::AccountIdOrAddress, account) \
    XX(GetAccountOrders, std::vector<api::MarketOrders>,                         \
        api::AccountIdOrAddress, account)                                        \
    XX(GetAccountOrdersAsset, api::MarketOrders,                                 \
        api::AccountIdOrAddress, account, api::AssetIdOrHash, asset)             \
    XX(GetTokenBalance, api::TokenBalanceLookup,                                 \
        api::AccountIdOrAddress, account, api::TokenIdOrSpec, token)             \
    XX(GetAccountHistory, api::AccountHistory,                                   \
        api::AccountIdOrAddress, address, uint64_t, beforeId)                    \
    XX(GetMempool, api::MempoolEntries)                                          \
    XX(GetBlockHash, Hash, Height, height)                                       \
    XX(GetRichlist, api::RichlistInfo, api::TokenIdOrSpec, token)

namespace chainserver {
DEFINE_TYPE_COLLECTION(APITypes, LIST_API_TYPES);
}

#undef LIST_API_TYPES

class ChainServer : public std::enable_shared_from_this<ChainServer>, public enable_api_methods<ChainServer, chainserver::APITypes> {
    using getBlocksCb = std::function<void(std::vector<BodyData>&&)>;
    friend enable_api_methods;

public:
    // can be called concurrently
    HeaderBatch get_headers(HeaderBatchSelector selector);
    wrt::optional<HeaderView> get_descriptor_header(Descriptor descriptor, Height height);
    ConsensusSlave get_chainstate();

    void shutdown();
    void wait_for_shutdown();

    // struct PutMempool {
    //     WartTransferCreate m;
    //     MempoolInsertCb callback;
    // };
    struct LookupTxids {
        Height maxHeight;
        std::vector<TransactionId> txids;
        MempoolTxsCb callback;
    };
    struct SetSynced {
        bool synced;
    };
    using SubscribeMining = mining_subscription::SubscriptionRequest;
    struct UnsubscribeMining {
        mining_subscription::SubscriptionId id;
    };
    struct GetBlocks {
        DescriptedBlockRange range;
        getBlocksCb callback;
    };
    struct GetRollbackBounds {
        using callback_t = std::function<void(market_history::RollbackBounds)>;

        NonzeroHeight height;
        Descriptor descriptor;
        callback_t callback;
    };
    struct GetBlockMarketHistory {
        using callback_t = std::function<void(market_history::BlockInfo)>;

        NonzeroHeight height;
        Descriptor descriptor;
        callback_t callback;
    };
    struct PutMempoolBatch {
        std::vector<TransactionMessage> txs;
    };
    struct SetSignedPin {
        SignedSnapshot ss;
    };

    // subscription related
    struct SubscribeAccount {
        SubscriptionRequest req;
        Address addr;
    };

    struct SubscribeChain : public SubscriptionRequest {
    };
    struct SubscribeMinerdist : public SubscriptionRequest {
    };

    struct DestroySubscriptions {
        subscription_data_ptr p;
    };

    // EVENTS
    using Event = events_t<
        // PutMempool,
        LookupTxids,
        SetSynced,
        SubscribeMining,
        UnsubscribeMining,
        GetBlocks,
        GetRollbackBounds,
        GetBlockMarketHistory,
        stage_operation::StageAddOperation,
        stage_operation::StageSetOperation,
        PutMempoolBatch,
        SetSignedPin,
        SubscribeAccount,
        SubscribeChain,
        SubscribeMinerdist,
        DestroySubscriptions>;

private:
    template <typename T>
    void defer(T&& e)
    {
        std::unique_lock l(mutex);
        haswork = true;
        events.push_back(std::forward<T>(e));
        cv.notify_one();
    }

    template <typename T>
    void defer_maybe_busy(T&& e)
    {
        std::unique_lock l(mutex);
        if (switching)
            e.callback(Error(ESWITCHING));
        else {
            haswork = true;
            events.emplace(std::forward<T>(e));
            cv.notify_one();
        }
    }

    struct Token { };

public:
    ChainServer(ChainDB& b, MarketDb* tdb, BatchRegistry&, wrt::optional<SnapshotSigner> snapshotSigner, Token);
    static auto make_chain_server(ChainDB& b, MarketDb* tdb, BatchRegistry& br, wrt::optional<SnapshotSigner> snapshotSigner)
    {
        return std::make_shared<ChainServer>(b, tdb, br, snapshotSigner, Token {});
    }
    void start()
    {
        assert(!worker.joinable());
        worker = std::thread(&ChainServer::work, this);
    }
    ~ChainServer();

    bool is_busy();

    template <typename Req>
    requires(MarketHistoryServer::supports<Req>)
    void api_call(Req&& req, Req::Callback cb)
    {
        if (marketServer) {
            marketServer->api_call(typename std::remove_cvref_t<Req>::Object(std::forward<Req>(req), std::move(cb)));
        } else {
            cb(Error(EAPINOTSUPPORTED));
        }
    }

    template <typename Req>
    requires(supports<Req>)
    void api_call(Req&& req, Req::Callback cb)
    {
        defer(typename std::remove_cvref_t<Req>::Object(std::forward<Req>(req), std::move(cb)));
    }

    void async_set_synced(bool synced);
    void async_put_mempool(std::vector<TransactionMessage> txs);

    // API methods
    [[nodiscard]] mining_subscription::MiningSubscription api_subscribe_mining(Address address, mining_subscription::callback_t callback);
    void api_unsubscribe_mining(mining_subscription::SubscriptionId);

    void subscribe_account_event(SubscriptionRequest, Address a);
    void subscribe_chain_event(SubscriptionRequest);
    void subscribe_minerdist_event(SubscriptionRequest);
    void destroy_subscriptions(subscription_data_ptr);

    void async_set_signed_checkpoint(SignedSnapshot);
    void async_get_blocks(DescriptedBlockRange, getBlocksCb&&);

    void get_rollback_bounds(NonzeroHeight, Descriptor, GetRollbackBounds::callback_t);
    void get_block_market_history(NonzeroHeight, Descriptor, GetBlockMarketHistory::callback_t cb);

    void async_stage_request(stage_operation::Operation);

private:
    ChainError apply_stage(ChainDBTransaction&& t);
    void work();
    void dispatch_mining_subscriptions();

    TxHash append_gentx(const TransactionCreate&);

private:
    auto handle_api(chainserver::PutMempool&&) -> TxHash;
    auto handle_api(chainserver::MiningAppend&& e) { return append_mined(e, true); };
    auto handle_api(chainserver::LatestTxs&&) { return state.api_get_latest_txs(); }
    auto handle_api(chainserver::LookupTxByHash&& e) { return state.api_get_tx(e.hash()); }
    auto handle_api(chainserver::GetTransactionMinfee&&) { return state.api_get_transaction_minfee(); }
    auto handle_api(chainserver::GetRichlist&& e) { return state.api_get_richlist(e.token(), 100); }
    auto handle_api(chainserver::GetTokenBalance&& e) { return state.api_get_token_balance_recursive(e.account(), e.token()); }
    auto handle_api(chainserver::GetWartBalance&& e) { return state.api_get_wart_balance(e.account()); }
    auto handle_api(chainserver::GetAccountOrders&& e) { return state.api_account_orders(e.account()); }
    auto handle_api(chainserver::GetAccountOrdersAsset&& e) { return state.api_account_orders_market(e.account(), e.asset()); }
    auto handle_api(chainserver::GetBlock&& e) { return state.api_get_block(e.heightOrHash()); }
    auto handle_api(chainserver::GetGrid&&) { return state.get_headers().grid(); }
    auto handle_api(chainserver::GetAccountMempool&& m) { return state.api_get_account_mempool(m.account(), 2000); }
    auto handle_api(chainserver::GetMempool&&) { return state.api_get_mempool(2000); }
    auto handle_api(chainserver::GetDBSize&&) { return api::DBSize { state.api_db_size() }; }
    auto handle_api(chainserver::GetHeader&& e) { return state.api_get_header(e.heightOrHash()); }
    auto handle_api(chainserver::GetBlockBinary&& e) { return state.api_get_block_binary(e.heightOrHash()); }
    auto handle_api(chainserver::LookupAsset&& e) { return state.api_get_asset(e.asset()); }
    auto handle_api(chainserver::CompleteAsset&& e) { return state.api_search_asset({ .namePrefix = e.namePrefix(), .hashPrefix = e.hashPrefix() }); }
    auto handle_api(chainserver::MarketDetail&& o) { return state.api_market_detail(o.asset()); }
    // auto handle_api(chainserver::GetOpenOrder&& o) { return state.api_market_detail(o.tx_hash()); }
    auto handle_api(chainserver::GetMining&& e) { return state.mining_task(e.address()); }
    auto handle_api(chainserver::GetTxcache&&) { return state.api_tx_cache(); }
    auto handle_api(chainserver::GetAccountHistory&& e) { return state.api_get_history(e.address(), e.beforeId()); }
    auto handle_api(chainserver::GetBlockHash&& e) { return state.get_hash(e.height()); }
    auto handle_api(chainserver::GetChainHead&&) { return state.api_get_head(); }
    auto handle_api(chainserver::MempoolConstraintUpdate&&) { return api::MempoolUpdate { .deletedTransactions = state.on_mempool_constraint_update() }; }
    auto handle_api(chainserver::FakeMine&& f) { return fake_mine(f.address()); }
    auto handle_api(chainserver::FakeMineToZero&&) { return fake_mine(Address::zero); }
    auto handle_api(chainserver::Rollback&&) { return api_rollback(); }

    // void handle_event(PutMempool&&);
    void handle_event(LookupTxids&&);
    void handle_event(SetSynced&& e);
    void handle_event(SubscribeMining&&);
    void handle_event(UnsubscribeMining&&);
    void handle_event(GetBlocks&&);
    void handle_event(GetRollbackBounds&&);
    void handle_event(GetBlockMarketHistory&&);
    void handle_event(stage_operation::StageSetOperation&&);
    void handle_event(stage_operation::StageAddOperation&&);
    void handle_event(PutMempoolBatch&&);
    void handle_event(SetSignedPin&&);
    void handle_event(SubscribeAccount&&);
    void handle_event(SubscribeChain&&);
    void handle_event(SubscribeMinerdist&&);
    void handle_event(DestroySubscriptions&&);

    void fake_mine(const Address&);
    void api_rollback();
    void append_mined(const chainserver::MiningAppend&, bool verifyPOW);
    using StateUpdateWithAPIBlocks = chainserver::state_update::StateUpdateWithAPIBlocks;
    void on_chain_changed(StateUpdateWithAPIBlocks&&);

    void emit_chain_state_event();

    std::optional<MarketHistoryServer> marketServer;

    std::condition_variable cv;
    ChainDB& db;
    BatchRegistry& batchRegistry;

    // state variables
    chainserver::State state;

    // mutex protected variables
    std::mutex mutex;
    using Events = std::vector<Event>;
    Events events;
    MiningSubscriptions miningSubscriptions;
    AddressSubscriptionState addressSubscriptions;
    ChainSubscriptionState chainSubscriptions;
    MinerdistSubscriptionState minerdistSubscriptions;

    //
    bool haswork = false;
    bool closing = false;
    bool switching = false; // doing chain switch?
    std::thread worker;
};
;

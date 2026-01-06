#pragma once

#include "api/types/forward_declarations.hpp"
#include "block/block_fwd.hpp"
#include "block/chain/height_header_work.hpp"
#include "block/chain/range.hpp"
#include "chainserver/state/helpers/cache.hpp"
#include "communication/messages.hpp"
#include "communication/mining_task.hpp"
#include "communication/stage_operation/result.hpp"
#include "defi/token/info.hpp"
#include "general/result.hpp"
#include "helpers/consensus.hpp"
#include "helpers/past_chains.hpp"
#include <chrono>

namespace chainserver {
struct MiningCache {
    using value_t = block::body::Body;
    struct CacheValidity {
        int db { 0 };
        int mempool { 0 };
        uint32_t timestamp;
        bool operator==(const CacheValidity&) const = default;
        CacheValidity(int db, int mempool, uint32_t timestamp)
            : db(db)
            , mempool(mempool)
            , timestamp(timestamp)
        {
        }
    };
    MiningCache(CacheValidity cacheValidity)
        : cacheValidity(cacheValidity)
    {
    }

    struct Item {
        Address address;
        bool disableTxs;
        value_t b;
    };

    CacheValidity cacheValidity;
    uint32_t timestamp;
    void update_validity(CacheValidity);
    [[nodiscard]] const value_t* lookup(const Address&, bool disableTxs) const;
    const value_t& insert(const Address& a, bool disableTxs, value_t);
    std::vector<Item> cache;
};

class State {
    friend class ApplyStageTransaction;
    friend class SetSignedPinTransaction;
    using StateUpdate = state_update::StateUpdate;
    using StateUpdateWithAPIBlocks = state_update::StateUpdateWithAPIBlocks;
    using StageUpdate = state_update::StageUpdate;
    using TxVec = std::vector<TransactionMessage>;

public:
    // constructor/destructor
    State(ChainDB& b, BatchRegistry&, wrt::optional<SnapshotSigner> snapshotSigner);

    // concurrent methods
    Batch get_headers_concurrent(BatchSelector selector) const;
    wrt::optional<HeaderView> get_header_concurrent(Descriptor descriptor, Height height) const;
    ConsensusSlave get_chainstate_concurrent();

    // normal methods
    void garbage_collect();
    [[nodiscard]] auto mining_task(const Address& a) -> Result<ChainMiningTask>;
    auto mining_task(const Address& a, bool disableTxs) -> Result<ChainMiningTask>;

    auto append_gentx(const TransactionCreate&) -> std::pair<mempool::Updates, TxHash>;
    auto chainlength() const -> Height { return chainstate.headers().length(); }

    // mempool
    [[nodiscard]] auto insert_txs(TxVec&&) -> std::pair<std::vector<Error>, mempool::Updates>;
    [[nodiscard]] size_t on_mempool_constraint_update();

    // stage methods
    auto set_stage(Headerchain&& hc) -> stage_operation::StageSetStatus;
    struct StageActionResult {
        stage_operation::StageAddStatus status;
        wrt::optional<RogueHeaderData> rogueHeaderData;
        wrt::optional<state_update::StateUpdateWithAPIBlocks> update;
    };
    auto add_stage(const std::vector<Block>& blocks, const Headerchain&) -> StageActionResult;

    // synced state notification
    void set_sync_state(bool synced)
    {
        if (synced) {
            signAfter = std::min(signAfter,
                std::chrono::steady_clock::now() + std::chrono::seconds(5));
        } else {
            signAfter = tp::max();
        }
    }

    // general getters
    auto get_header(Height h) const -> wrt::optional<api::HeaderInfo>;
    auto get_headers() const { return chainstate.headers(); }
    auto get_hash(Height h) const -> wrt::optional<Hash>;
    auto get_body_data(DescriptedBlockRange) const -> std::vector<BodyData>;
    auto get_mempool_tx(TransactionId) const -> wrt::optional<TransactionMessage>;

    // api getters
    api::WartBalanceLookup api_get_wart_balance(api::AccountIdOrAddress a) const;
    Result<api::TokenBalanceLookup> api_get_token_balance_recursive(api::AccountIdOrAddress a, api::TokenIdOrSpec t) const;
    auto api_get_head() const -> api::ChainHead;
    auto api_get_history(const api::AccountIdOrAddress& a, int64_t beforeId = 0x7fffffffffffffff) const -> wrt::optional<api::AccountHistory>;
    auto api_get_richlist(api::TokenIdOrSpec token, size_t limit) const -> Result<api::RichlistInfo>;
    auto api_get_mempool(size_t) const -> api::MempoolEntries;
    auto api_get_tx(const TxHash& hash) const -> wrt::optional<api::Transaction>;
    auto api_get_transaction_minfee() -> api::TransactionMinfee;
    auto api_get_latest_txs(size_t N = 100) const -> api::TransactionsByBlocks;
    auto api_get_latest_blocks(size_t N = 100) const -> api::TransactionsByBlocks;
    auto api_get_miner(NonzeroHeight h) const -> wrt::optional<api::Account>;
    auto api_get_latest_miners(uint32_t N = 1000) const -> std::vector<api::Account>;
    auto api_get_miners(HeightRange) const -> std::vector<api::Account>;
    auto api_get_transaction_range(HistoryId lower, HistoryId upper) const -> api::TransactionsByBlocks;
    auto api_get_header(const api::HeightOrHash& h) const -> Result<api::HeaderInfo>;
    auto api_complete_token(std::string_view prefix) const -> Result<api::AssetPrefixList>;

    auto api_get_block(const api::HeightOrHash& h) const -> Result<api::Block>;
    auto api_get_block_binary(const api::HeightOrHash& h) const -> wrt::optional<api::BlockBinary>;
    auto api_tx_cache() const -> const TransactionIds;
    size_t api_db_size() const;

private:
    using NormalizedToken = api::Token;
    [[nodiscard]] wrt::optional<NormalizedToken> normalize(api::TokenIdOrSpec) const;
    [[nodiscard]] wrt::optional<api::Account> normalize(api::AccountIdOrAddress) const;
    // wrt::optional<AssetDetail> db_lookup_token(const api::AssetIdOrHash&) const;
    // delegated getters
    auto api_get_block(Height h) const -> wrt::optional<api::Block>;
    auto api_get_block_binary(Height h) const -> wrt::optional<api::BlockBinary>;
    struct BalanceLookup {
        wrt::optional<api::AssetLookupTrace> lookupTrace;
        api::FundsBalance balance;
    };

    BalanceLookup api_get_token_balance_recursive(AccountId, TokenId) const;
    wrt::optional<NonzeroHeight> consensus_height(const Hash&) const;
    NonzeroHeight next_height() const { return chainlength().add1(); }

    // transactions

    struct ApplyStageResult {
        stage_operation::StageAddStatus status;
        wrt::optional<Worksum> errorWorksum; // has value when status is not 0
        wrt::optional<Header> errorHeader; // has value when status is not 0
        wrt::optional<state_update::StateUpdateWithAPIBlocks> update;
    };
    [[nodiscard]] auto apply_stage(ChainDBTransaction&& t) -> ApplyStageResult;

public:
    [[nodiscard]] auto apply_signed_snapshot(SignedSnapshot&& sp) -> wrt::optional<StateUpdateWithAPIBlocks>;
    //  stageUpdate;
    [[nodiscard]] auto append_mined_block(const Block&, bool verifyPOW = true) -> StateUpdateWithAPIBlocks;

private:
    const AssetDetail* lookup_hash_warn(const AssetHash&) const;
    wrt::optional<api::Transaction> api_dispatch_mempool(const TxHash&, TransactionMessage&&) const;
    api::Transaction api_dispatch_history(const TxHash&, HistoryId hid, history::HistoryVariant&&, NonzeroHeight) const;

    // transaction helpers
    [[nodiscard]] chainserver::RollbackResult rollback(const Height newlength) const;

    // finalize helpers
    [[nodiscard]] auto commit_fork(RollbackResult&& rr, AppendBlocksResult&&) -> StateUpdate;
    [[nodiscard]] auto commit_append(AppendBlocksResult&& abr) -> StateUpdate;
    wrt::optional<SignedSnapshot> try_sign_locked_chainstate();
    MiningCache::CacheValidity mining_cache_validity() const;

private:
    using tp = std::chrono::steady_clock::time_point;
    ChainDB& db;
    mutable DBCache dbcache;
    BatchRegistry& batchRegistry;

    wrt::optional<SnapshotSigner> snapshotSigner;
    wrt::optional<SignedSnapshot> signedSnapshot;

    int dbCacheValidity { 0 };
    tp signAfter { tp::max() };
    bool signingEnabled { true };

    mutable std::mutex chainstateMutex; // protects writes to blockCashe and chainstate (descriptor and headers) for "_concurrent" methods
    BlockCache blockCache;
    chainserver::Chainstate chainstate;

    ExtendableHeaderchain stage;
    std::chrono::steady_clock::time_point nextGarbageCollect;

    MiningCache _miningCache;
};
}

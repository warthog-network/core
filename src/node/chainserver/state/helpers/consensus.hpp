#pragma once
#include "../../transaction_ids.hpp"
#include "../update/update.hpp"
#include "block/body/account_id.hpp"
#include "block/chain/consensus_headers.hpp"
#include "block/chain/history/index.hpp"
#include "block/chain/offsts.hpp"
#include "cache_fwd.hpp"
#include "chainserver/db/deletion_key.hpp"
#include "chainserver/db/state_ids.hpp"
#include "chainserver/db/types_fwd.hpp"
#include "chainserver/types.hpp"
#include "communication/create_transaction_fwd.hpp"
#include "mempool/mempool.hpp"
#include <cstdint>

namespace chainserver {
struct RollbackResult {
    ShrinkInfo shrink;
    std::vector<TransactionMessage> toMempool;
    FreeBalanceUpdates freeBalanceUpdates;
    TransactionIds chainTxIds;
    DeletionKey deletionKey;
};
struct AppendBlocksResult {
    FreeBalanceUpdates freeBalanceUpdates;
    std::vector<HistoryId> newHistoryOffsets;
    std::vector<StateId64> stateOffsets;
    TransactionIds newTxIds;
};

struct Chainstate {
    struct RollbackData {
        RollbackResult&& rollbackResult;
    };
    struct ForkData {
        ExtendableHeaderchain stage;
        RollbackResult&& rollbackResult;
        AppendBlocksResult&& appendResult;
    };
    struct AppendMulti {
        ExtendableHeaderchain patchedChain;
        AppendBlocksResult&& appendResult;
    };
    struct AppendSingle {
        FreeBalanceUpdates freeBalanceUpdates;
        wrt::optional<SignedSnapshot>& signedSnapshot;
        HeaderVerifier::PreparedAppend prepared;
        TransactionIds&& newTxIds;
        HistoryId newHistoryOffset;
        StateId64 newStateOffset;
    };
    Chainstate(const ChainDB& db, BatchRegistry& br);

    // modification functions
    mempool::Updates pop_mempool_updates();

    using Update = state_update::StateUpdate;

    void fork(ForkData&&);
    [[nodiscard]] auto rollback(const RollbackResult&) -> HeaderchainRollback;
    [[nodiscard]] auto append(AppendMulti) -> HeaderchainAppend;
    [[nodiscard]] auto append(AppendSingle) -> HeaderchainAppend;

    auto insert_txs(std::vector<TransactionMessage>&& txs) -> std::vector<Error>;

    size_t on_mempool_constraint_update();
    TxHash insert_tx_throw(TransactionMessage&& m, DBCache& ac);
    [[nodiscard]] TxHash create_tx_throw(const TransactionCreate& m);

    // const functions
    Worksum work_with_new_block() const { return headerchain.total_work() + headerchain.next_target(); };
    const auto& headers() const { return headerchain; }
    auto mining_data() const { return headerchain.mining_data(); };
    const Hash& final_hash() const { return headerchain.final_hash(); }
    const Hash& hash_at(Height h) const { 
        if (h==headerchain.length()) 
            return headerchain.final_hash(); 
        auto hash{headerchain.get_hash(h)};
        if (!hash) 
            headerchain.throw_out_of_bounds(h);
        return *hash;
    }

    auto prepare_append(const wrt::optional<SignedSnapshot>& sp, HeaderView hv, bool verifyPOW) const { return headerchain.prepare_append(sp, hv, verifyPOW); }
    Height length() const { return headerchain.length(); }
    Descriptor descriptor() const { return dsc; }
    const auto& txids() const { return chainTxIds; }
    const auto& mempool() const { return _mempool; }
    [[nodiscard]] inline auto history_lower_bound(NonzeroHeight height) const
    {
        if (height.value() == 1)
            return HistoryId::smallest();
        return historyOffsets.at(height);
    };
    NonzeroHeight history_height(HistoryId historyIndex) const
    {
        return historyOffsets.height(historyIndex);
    }
    StateId64 state_lower_bound(NonzeroHeight height) const{
        if (height.value() == 1)
            return StateId64(0);
        return stateOffsets.at(height);
    }
    [[nodiscard]] StateHeight state_height(AssetId id) const
    {
        return stateOffsets.height(state_id(id));
    }
    StateHeight state_height(AccountId id) const
    {
        return stateOffsets.height(state_id(id));
    }

protected:
    PinHash pin_hash(PinHeight ph) const;
    WartTransferMessage create_specific_tx(const TransactionId&, const WartTransferCreate& m);
    TokenTransferMessage create_specific_tx(const TransactionId&, const TokenTransferCreate& m);
    LimitSwapMessage create_specific_tx(const TransactionId&, const LimitSwapCreate& m);
    LiquidityDepositMessage create_specific_tx(const TransactionId&, const LiquidityDepositCreate& m);
    LiquidityWithdrawalMessage create_specific_tx(const TransactionId&, const LiquidityWithdrawalCreate& m);
    CancelationMessage create_specific_tx(const TransactionId&, const CancelationCreate& m);
    AssetCreationMessage create_specific_tx(const TransactionId&, const AssetCreationCreate& m);

    TxHeight tx_height(TransactionMessage&&, TxHash, const Address& fromAddr, DBCache&);
    TxHash insert_tx_internal_throw(TransactionMessage&&, TxHash, const Address& fromAddr, DBCache&);
    void prune_txids();
    void update_free_balances(const FreeBalanceUpdates& updates);
    Chainstate(std::tuple<std::vector<HeaderBatch>, HistoryHeights, State64Heights> init,
        const ChainDB& db, BatchRegistry& br);

private: // private methods
    void update_allowed_mempool_transaction_types();
    void set_allowed_mempool_transaction_types();

private:
    const ChainDB& db;
    uint32_t dsc = 0;
    void assert_equal_length();
    ExtendableHeaderchain headerchain;
    BlockVersion nextBlockversion;
    HistoryHeights historyOffsets;
    State64Heights stateOffsets;
    TransactionIds chainTxIds; // replay protection
    mempool::Mempool _mempool;
};

}

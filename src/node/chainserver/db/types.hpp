#pragma once

#include "block/body/account_id.hpp"
#include "block/body/transaction_id.hpp"
#include "block/chain/height.hpp"
#include "block/chain/history/index.hpp"
#include "defi/token/id.hpp"
#include "defi/token/asset.hpp"
#include "defi/uint64/pool.hpp"
#include "defi/uint64/price.hpp"
#include "types_fwd.hpp"
namespace chain_db {
struct AssetData {
    AssetId id;
    NonzeroHeight height;
    AccountId ownerAccountId;
    FundsDecimal supply;
    TokenId groupId;
    TokenId parentId;
    AssetName name;
    AssetHash hash;
    std::vector<uint8_t> data;
};
struct OrderDelete {
    HistoryId id;
    bool buy;
};
struct OrderFillstate {
    HistoryId id;
    Funds_uint64 filled;
    bool buy;
    static constexpr size_t byte_size()
    {
        return HistoryId::byte_size() + Funds_uint64::byte_size() + 1;
    }
    void serialize(RawSerializer auto&& s) const
    {
        s << id << filled << buy;
    }
};
struct AccountData {
    AccountId id;
    AddressView address;
};
struct BalanceDataWithoutId {
    AccountId accountId;
    TokenId tokenId;
    Funds_uint64 total;
    Funds_uint64 locked;
};

struct BalanceData: public BalanceDataWithoutId {
    BalanceId id;
};

struct TokenForkBalanceData {
    TokenForkBalanceId id;
    AccountId accountId;
    AssetId assetId;
    Height height;
    Funds_uint64 balance;
};
struct OrderData {
    HistoryId id;
    bool buy;
    TransactionId txid;
    AssetId aid;
    Funds_uint64 total;
    Funds_uint64 filled;
    Funds_uint64 remaining() const
    {
        return diff_assert(total, filled);
    }
    TokenId spend_token_id() const
    {
        if (buy)
            return TokenId::WART;
        return aid.token_id();
    }

    Price_uint64 limit;
    static constexpr size_t byte_size()
    {
        return HistoryId::byte_size() + 1 + TransactionId::byte_size() + AssetId::byte_size() + 2 * Funds_uint64::byte_size() + Price_uint64::byte_size();
    }
    void serialize(RawSerializer auto&& s) const
    {
        s << id << buy << txid << aid << total << filled << limit;
    }
};
struct PoolData : public defi::Pool_uint64 {
    struct Initializer {
        AssetId assetId;
        Funds_uint64 base;
        Funds_uint64 quote;
        Funds_uint64 shares;
    };
    PoolData(AssetId assetId, defi::Pool_uint64 pool)
        : defi::Pool_uint64(std::move(pool))
        , assetId(assetId)
    {
    }
    PoolData(Initializer i)
        : defi::Pool_uint64(i.base, i.quote, i.shares)
        , assetId(i.assetId)
    {
    }
    static PoolData zero(AssetId assetId)
    {
        return Initializer {
            .assetId { assetId },
            .base { 0 },
            .quote { 0 },
            .shares { 0 }
        };
    }

    auto asset_id() const { return assetId; }

private:
    AssetId assetId;
};
}

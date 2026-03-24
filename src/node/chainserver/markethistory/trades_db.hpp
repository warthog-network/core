#pragma once
#include "SQLiteCpp/Database.h"
#include "SQLiteCpp/Transaction.h"
#include "api_types.hpp"
#include "block/chain/height.hpp"
#include "request_types.hpp"
#include "crypto/hash.hpp"
#include "db/sqlite_fwd.hpp"
#include "defi/token/id.hpp"
#include "general/funds.hpp"
#include "general/timestamp.hpp"
#include "wrt/variant.hpp"
namespace market_history {

struct Asset {
    AssetId id;
    AssetHash hash;
    Height latestHeight;
};

// class TradeAmount {
// protected:
//     double _base;
//     double _quote;
//     TradeAmount(double base, double quote)
//         : _base { base }
//         , _quote { quote }
//     {
//     }
//
// public:
//     double base() const { return _base; }
//     double quote() const { return _quote; }
//     double price() const
//     {
//         return _quote / _base;
//     }
//     [[nodiscard]] static wrt::optional<TradeAmount> create(double base, double quote)
//     {
//         if (base == 0 || quote == 0)
//             return {};
//         return TradeAmount { base, quote };
//     }
// };

struct Trade : public TradeAmount {
    NonzeroHeight height;

    std::optional<Trade> nonzero(NonzeroHeight height, FundsDecimal base, Wart quote)
    {
        if (auto ta { TradeAmount::create(base.to_double(), quote.to_double()) })
            return Trade(*ta, height);
        return {};
    }

    Trade(TradeAmount amount, Height height)
        : TradeAmount(amount)
        , height(height)
    {
    }
};

struct FIVEMIN {
    constexpr FIVEMIN() { };
    static inline constexpr const char* slug = "5m";
    static const uint32_t seconds = 5 * 60;
};
struct ONEHOUR {
    constexpr ONEHOUR() { };
    static inline constexpr const char* slug = "1h";
    static const uint32_t seconds = 60 * 60;
};
struct ONEDAY {
    constexpr ONEDAY() { };
    static inline constexpr const char* slug = "1d";
    static const uint32_t seconds = 24 * 60 * 60;
};
namespace impl {

template <typename Derived, typename... Ts>
struct interval_variant : public wrt::variant<Ts...> {
    using wrt::variant<Ts...>::variant;
    interval_variant() = delete;
    [[nodiscard]] static constexpr std::optional<Derived> from_slug(std::string_view slug)
    {
        std::optional<Derived> out;
        ([&] {
            if (Ts::slug == slug) {
                out = Ts();
                return false;
            }
            return true;
        }() && ...);
        return out;
    }

    constexpr auto seconds() const
    {
        return this->visit([](auto&& i) { return i.seconds; });
    }
    constexpr auto slug() const
    {
        return this->visit([](auto&& i) { return i.slug; });
    }
};
}

struct Interval : public impl::interval_variant<Interval, FIVEMIN, ONEHOUR, ONEDAY> {
    using interval_variant::interval_variant;
};

using api::Candle;
using api::CandlesVector;
using api::TradesVector;

using Statement = sqlite::Statement;
class MarketReaderDB {
public:
    [[nodiscard]] Height chain_length() const;
    [[nodiscard]] wrt::optional<Asset> get_asset(AssetId) const;
    [[nodiscard]] wrt::optional<Asset> get_asset(AssetHash) const;

    TradesVector get_trades_range(AssetId, NonzeroHeight from, NonzeroHeight to) const;
    TradesVector get_trades_from(AssetId, NonzeroHeight from, size_t n) const;
    TradesVector get_trades_to(AssetId, NonzeroHeight to, size_t n) const;
    TradesVector get_trades_latest(AssetId, size_t n) const;

    CandlesVector get_candles_range(AssetId, Interval interval, Timestamp from, Timestamp to) const;
    CandlesVector get_candles_from(AssetId, Interval interval, Timestamp from, size_t n) const;
    CandlesVector get_candles_to(AssetId, Interval interval, Timestamp to, size_t n) const;
    CandlesVector get_candles_latest(AssetId, Interval interval, size_t n) const;

    [[nodiscard]] wrt::optional<BlockHash> get_block_hash(NonzeroHeight height) const;
    MarketReaderDB clone_reader() const;
    [[nodiscard]] auto transaction() const { return SQLite::Transaction(db); }

protected:
    template <typename... Args>
    [[nodiscard]] std::vector<Candle> extract_candles(AssetId, Interval, std::string_view condition, Args&&... args) const;
    template <typename... Args>
    [[nodiscard]] std::vector<api::Trade> extract_trades(AssetId, std::string_view condition, Args&&... args) const;
    MarketReaderDB(SQLite::Database&& db);
    mutable SQLite::Database db;

private:
    mutable Statement stmtSelectAssetById;
    mutable Statement stmtSelectAssetByHash;
    mutable Statement stmtSelectMaxHeight;
    mutable Statement stmtSelectBlock;
};


class MarketDB : public MarketReaderDB {
    struct CandleBegin {
        NonzeroHeight height;
        Timestamp timestamp;
    };

public:
    MarketDB(const std::string& path);

    void append_block(const BlockInfo& blockInfo);
    void clear();
    void rollback(const RollbackBounds&);
    Height chain_length()
    {
        return length;
    }

private:
    void insert_asset(AssetId assetId, AssetHash hash);
    void insert_trade(const Asset& asset, const Trade&, Timestamp ts);
    void delete_assets_from(AssetId assetId, NonzeroHeight height);

    void asset_rollback(AssetId assetId, NonzeroHeight blockHeight, Timestamp blockTime);

    // after rollback, candles need to be rebuilt because we don't
    // know if we have removed the trades that defined candle high and low.
    void asset_rebuild_candles(AssetId, CandleBegin);

    // The following function shall delete candles that contain trades started from specified height. The candles are sorted by timestamp (5 minute rounded) and each candle saves the first height with timestamp greater than or equal to the candle begin timestamp. This implies that trades with greater height will be contained in the same or later candle such that we can exploit the timestamp indexing ot the candle table. If we pass the block timestamp of the same or some earlier height we can use the index to scan candles in timestamp order from this timestamp and easily find first candle containing the passed height.
    [[nodiscard]] CandleBegin asset_erase_candles_from(AssetId, NonzeroHeight blockHeight, Timestamp blockTimestamp);
    size_t erase_trades_from_height(AssetId, NonzeroHeight from);
    void erase_interval_candles_from_height(AssetId, Interval interval, Timestamp from);
    void asset_drop_tables(AssetId assetId);

    [[nodiscard]] wrt::optional<Candle> get_latest_candle(AssetId, Interval) const;

    void aggregate_into_candles(AssetId, Interval, const Trade t, Timestamp ts);

    void create_tables(AssetId asset);
    void insert_candle(AssetId, Interval, const Candle&);

    void insert_block(NonzeroHeight height, BlockHash hash, Timestamp);
    void delete_block_from(NonzeroHeight height);
    wrt::optional<BlockHash> select_block(NonzeroHeight height);

private:
    Statement stmtSetLatestHeight;
    Statement stmtInsertAsset;

    Statement stmtInsertBlock;
    Statement stmtDeleteBlockFrom;

    mutable Statement stmtSelectAssetsFrom;
    Statement stmtDeleteAssets;
    mutable Statement stmtSelectAssetIdsByLatestHeight;
    Height length;
};
};
using MarketDb = market_history::MarketDB;

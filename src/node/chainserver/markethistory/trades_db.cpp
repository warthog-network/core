#include "trades_db.hpp"
#include "db/sqlite.hpp"
#include "spdlog/spdlog.h"
namespace market_history {

namespace {
SQLite::Database create_database(const std::string& path)
{
    spdlog::debug("Opening market database \"{}\"", path);
    SQLite::Database out(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    out.exec(
        "PRAGMA foreign_keys = ON;"
        "PRAGMA journal_mode = WAL;"
        "CREATE TABLE IF NOT EXISTS `Assets` (`id` INTEGER NOT NULL, `hash` INTEGER UNIQUE, `latestHeight` INTEGER NOT NULL, PRIMARY KEY(`id`));"
        "CREATE TABLE IF NOT EXISTS `Blocks` (`height` INTEGER, `hash` INTEGER UNIQUE, `timestamp` INTEGER, PRIMARY KEY(`height`));"
        "CREATE INDEX IF NOT EXISTS `latestHeightIndex` ON `Assets` ( `latestHeight`);");
    return out;
}

[[nodiscard]] inline std::string candles_table(AssetId assetId, Interval interval)
{
    return std::format("Candles_{}_{}", assetId.value(), interval.slug());
}

[[nodiscard]] inline std::string trades_table(AssetId assetId)
{
    return std::format("Trades_{}", assetId.value());
}
void candle_add_trade(Candle& c, const TradeAmount& ta)
{
    auto price { ta.price() };
    c.close = price;
    if (c.high < price)
        c.high = price;
    if (c.low > price)
        c.low = price;
    c.base += ta.base();
    c.quote += ta.quote();
}
}

MarketReaderDB::MarketReaderDB(SQLite::Database&& dbtmp)
    : db(std::move(dbtmp))
    , stmtSelectAssetById(db, "SELECT id, hash, latestHeight FROM Assets WHERE id = ?")
    , stmtSelectAssetByHash(db, "SELECT id, hash, latestHeight FROM Assets WHERE hash = ?")
    , stmtSelectMaxHeight(db, "SELECT height FROM Blocks ORDER BY height DESC LIMIT 1")
    , stmtSelectBlock(db, "SELECT hash FROM Blocks WHERE height = ?")
{
}

Height MarketReaderDB::chain_length() const
{
    return stmtSelectMaxHeight.one().process([](const sqlite::Row& row) {
                                        return Height(row[0]);
                                    })
        .value_or(Height(0));
}
wrt::optional<Asset> MarketReaderDB::get_asset(AssetId id) const
{
    return stmtSelectAssetById.one(id).process([](auto& row) {
        return Asset { .id = row[0], .hash = row[1], .latestHeight = row[2] };
    });
}
wrt::optional<Asset> MarketReaderDB::get_asset(AssetHash hash) const
{
    return stmtSelectAssetByHash.one(hash).process(
        [](auto& row) { return Asset {
                            .id = row[0],
                            .hash = row[1],
                            .latestHeight = row[2]
                        }; });
}

template <typename... Args>
inline std::vector<api::Trade> MarketReaderDB::extract_trades(AssetId assetId, std::string_view condition, Args&&... args) const
{
    auto table{trades_table(assetId)};
    auto query = std::format("SELECT {}.height AS height, timestamp, base, quote FROM {} JOIN Blocks ON {}.height = Blocks.height {}", table, table, table, condition);
    Statement stmt(db, query);
    return stmt.all([](const sqlite::Row& row) {
        return api::Trade {
            .timestamp = row[1],
            .height = row[0],
            .base = row[2],
            .quote = row[3]
        };
    },
        std::forward<Args>(args)...);
}
TradesVector MarketReaderDB::get_trades_range(AssetId aid, NonzeroHeight from, NonzeroHeight to) const
{
    return { .elements = extract_trades(aid, "WHERE height >= ? AND height <=? ORDER BY height ASC", from, to), .reverse = false };
}
TradesVector MarketReaderDB::get_trades_from(AssetId aid, NonzeroHeight from, size_t n) const
{
    return { .elements = extract_trades(aid, "WHERE height >= ? ORDER BY height ASC LIMIT ?", from, n), .reverse = false };
}
TradesVector MarketReaderDB::get_trades_to(AssetId aid, NonzeroHeight to, size_t n) const
{
    return { .elements = extract_trades(aid, "WHERE height <= ? ORDER BY height DESC LIMIT ?", to, n), .reverse = true };
}
TradesVector MarketReaderDB::get_trades_latest(AssetId aid, size_t n) const
{
    return { .elements = extract_trades(aid, "ORDER BY height DESC LIMIT ?", n), .reverse = true };
}

template <typename... Args>
inline std::vector<Candle> MarketReaderDB::extract_candles(AssetId assetId, Interval interval, std::string_view condition, Args&&... args) const
{
    auto query = std::format("SELECT timestamp, height, open, high, low, close, base, quote FROM {} {}", candles_table(assetId, interval), condition);
    Statement stmt(db, query);
    return stmt.all([](const sqlite::Row& row) {
        return Candle {
            .timestamp = row[0],
            .height = row[1],
            .open = row[2],
            .high = row[3],
            .low = row[4],
            .close = row[5],
            .base = row[6],
            .quote = row[7],
        };
    },
        std::forward<Args>(args)...);
}

CandlesVector MarketReaderDB::get_candles_range(AssetId aid, Interval interval, Timestamp from, Timestamp to) const
{
    return { .elements = extract_candles(aid, interval, "WHERE timestamp >= ? AND timestamp <=? ORDER BY timestamp ASC", from, to), .reverse = false };
}
CandlesVector MarketReaderDB::get_candles_from(AssetId aid, Interval interval, Timestamp from, size_t n) const
{
    return { .elements = extract_candles(aid, interval, "WHERE timestamp >= ? ORDER BY timestamp ASC LIMIT ?", from, n), .reverse = false };
}
CandlesVector MarketReaderDB::get_candles_to(AssetId aid, Interval interval, Timestamp to, size_t n) const
{
    return { .elements = extract_candles(aid, interval, "WHERE timestamp <= ? ORDER BY timestamp DESC LIMIT ?", to, n), .reverse = true };
}
CandlesVector MarketReaderDB::get_candles_latest(AssetId aid, Interval interval, size_t n) const
{
    return { .elements = extract_candles(aid, interval, "ORDER BY timestamp DESC LIMIT ?", n), .reverse = true };
}

wrt::optional<BlockHash> MarketReaderDB::get_block_hash(NonzeroHeight height) const
{
    return stmtSelectBlock.one(height).process([](const sqlite::Row& row) {
        return BlockHash(row[0]);
    });
}

MarketReaderDB MarketReaderDB::clone_reader() const
{
    return { SQLite::Database(db.getFilename(), SQLite::OPEN_READONLY) };
}

void MarketDB::aggregate_into_candles(AssetId assetId, Interval interval, const Trade tr, Timestamp ts)
{
    auto c { get_latest_candle(assetId, interval) };
    auto begin { ts.floor(interval.seconds()) };
    if (c && c->timestamp >= begin) { // update old candle
        candle_add_trade(*c, tr);
        auto table { candles_table(assetId, interval) };
        Statement stmt(db, std::format("UPDATE {} SET high=?, low=?, close=?, base=?, quote=? WHERE timestamp=?", table));
        stmt.run(c->high, c->low, c->close, c->base, c->quote, c->timestamp);
    } else {
        double price { tr.price() };
        Candle c {
            .timestamp = begin,
            .height { tr.height },
            .open = price,
            .high = price,
            .low = price,
            .close = price,
            .base = tr.base(),
            .quote = tr.quote(),
        };
        insert_candle(assetId, interval, c);
    }
}

void MarketDB::append_block(const BlockInfo& blockInfo)
{
    assert(blockInfo.height == length + 1);
    insert_block(blockInfo.height, blockInfo.hash, blockInfo.timestamp);
    length = blockInfo.height;
    for (auto& asset : blockInfo.newAssets) {
        insert_asset(asset.id, asset.hash);
    }
    for (auto& t : blockInfo.trades) {
        auto asset { get_asset(t.assetId) };
        assert(asset.has_value());
        insert_trade(*asset, { t, blockInfo.height }, blockInfo.timestamp);
    }
}

void MarketDB::insert_trade(const Asset& asset, const Trade& tr, Timestamp ts)
{
    if (asset.latestHeight.is_zero())
        create_tables(asset.id);
    auto table { trades_table(asset.id) };
    Statement stmt(db, std::format("INSERT INTO {} (height, base, quote) VALUES (?, ?, ?)", table));
    stmt.run(tr.height, tr.base(), tr.quote());
    aggregate_into_candles(asset.id, FIVEMIN(), tr, ts);
    aggregate_into_candles(asset.id, ONEHOUR(), tr, ts);
    aggregate_into_candles(asset.id, ONEDAY(), tr, ts);
    stmtSetLatestHeight.run(tr.height, asset.id);
}

void MarketDB::clear()
{
    rollback(RollbackBounds {
        .assetIdDeleteFrom { 0 },
        .length { Height(0).add1() },
        .timestamp { 0 },
    });
}

void MarketDB::rollback(const RollbackBounds& rb)
{
    if (rb.length >= length)
        return;
    auto nextHeight { rb.length.add1() };

    // assets which were inserted starting from nextAssetId can be deleted directly
    delete_assets_from(rb.assetIdDeleteFrom, nextHeight);

    // for the remaining assets, roll back those that have latestHeight>= next.height
    stmtSelectAssetIdsByLatestHeight.for_each(
        [&](auto& row) {
            AssetId id(row[0]);
            asset_rollback(id, nextHeight, rb.timestamp);
        },
        rb.length);
    delete_block_from(nextHeight);
    length = rb.length;
}

void MarketDB::erase_interval_candles_from_height(AssetId assetId, Interval interval, Timestamp from)
{
    auto table { candles_table(assetId, interval) };
    sqlite::Statement stmt(db, std::format("DELETE FROM {} WHERE timestamp >= ?", table));
    stmt.run(from);
}

auto MarketDB::asset_erase_candles_from(AssetId assetId, NonzeroHeight blockHeight, Timestamp timestampOfPrevBlock) -> CandleBegin
{
    std::optional<CandleBegin> cb;
    const auto tableName { candles_table(assetId, FIVEMIN()) };
    {
        sqlite::Statement stmt(db, std::format("SELECT timestamp, height FROM {} WHERE timestamp>=? ORDER BY timestamp ASC", tableName));
        stmt.for_each_while([&](auto&& row) {
            auto intervalHeight { Height(row[1]).nonzero() };
            assert(intervalHeight.has_value());
            if (*intervalHeight <= blockHeight) {
                cb = CandleBegin {
                    .height = row[1],
                    .timestamp = row[0],
                };
                return true; // continue;
            }
            return false; // stop the for_each_continue loop.
        },
            timestampOfPrevBlock.floor(FIVEMIN::seconds));
    } // local scope end
    assert(cb.has_value()); // there must be values in the candle containing the trade to which the passed args corresponsd.
    //
    auto deleteFromTimestamp { cb->timestamp };
    sqlite::Statement stmt(db, std::format("DELETE FROM {} WHERE timestamp >= ?)", tableName));
    stmt.run(deleteFromTimestamp);
    erase_interval_candles_from_height(assetId, FIVEMIN(), deleteFromTimestamp);
    erase_interval_candles_from_height(assetId, ONEHOUR(), deleteFromTimestamp.floor(ONEHOUR::seconds));
    erase_interval_candles_from_height(assetId, ONEDAY(), deleteFromTimestamp.floor(ONEDAY::seconds));

    return *cb;
}

size_t MarketDB::erase_trades_from_height(AssetId assetId, NonzeroHeight from)
{
    auto table { trades_table(assetId) };
    sqlite::Statement stmt(db, std::format("DELETE FROM {} WHERE height >= ?", table));
    return stmt.run(from);
}

void MarketDB::asset_rebuild_candles(AssetId assetId, CandleBegin cb)
{
    // rebuild 5min candles from trades
    std::optional<Candle> c;
    { // rebuild 5 min candle from trades
        Statement stmt(db, std::format("SELECT base, quote FROM {} WHERE height >= ?", trades_table(assetId)));
        stmt.for_each([&](auto& row) {
            double base { row[0] };
            double quote { row[1] };
            auto ta(TradeAmount::create(base, quote));
            assert(ta.has_value()); // should not be zero trade
            if (!c) {
                auto price { ta->price() };
                c = Candle {
                    .timestamp { cb.timestamp },
                    .height { cb.height },
                    .open = price,
                    .high = price,
                    .low = price,
                    .close = price,
                    .base = base,
                    .quote = quote
                };
            } else {
                candle_add_trade(*c, *ta);
            }
        },
            cb.height);
        if (!c.has_value()) {
            // Deleted trades were exactly deleting one candle
            // (no other trades in that candle)
            return;
        }
        insert_candle(assetId, FIVEMIN(), *c);
    }

    auto aggregate_candles_from { [&](Interval inInterval, Interval outInterval) { // now rebuild 1 hour candle from 5 min candles
        auto outBegin = c->timestamp.floor(outInterval.seconds());
        c.reset();
        Statement stmt(db, std::format("SELECT height, open, high, low, close, base, quote FROM {} WHERE timestamp>=?", candles_table(assetId, inInterval)));
        stmt.for_each([&](const sqlite::Row& row) {
            NonzeroHeight height { row[0] };
            double open { row[1] };
            double high { row[2] };
            double low { row[3] };
            double close { row[4] };
            double base { row[5] };
            double quote { row[6] };
            if (!c) {
                c = Candle {
                    .timestamp = outBegin,
                    .height = height,
                    .open = open,
                    .high = high,
                    .low = low,
                    .close = close,
                    .base = base,
                    .quote = quote,
                };
            } else {
                if (c->high < high)
                    c->high = high;
                if (c->low > low)
                    c->low = low;
                c->close = close;
                c->base += base;
                c->quote += quote;
            }
        },
            outBegin);
        assert(c.has_value());
        // now insert into database
        insert_candle(assetId, outInterval, *c);
    } };
    aggregate_candles_from(FIVEMIN(), ONEHOUR());
    aggregate_candles_from(ONEHOUR(), ONEDAY());
}

void MarketDB::asset_rollback(AssetId assetId, NonzeroHeight nextHeight, Timestamp timestampAtRollbackHeight)
{
    auto erased { erase_trades_from_height(assetId, nextHeight) };
    if (erased == 0)
        return;
    // We erased some trades, so there must be candles to delete.

    Statement stmt(db, std::format("SELECT height FROM {} ORDER BY height DESC LIMIT 1", trades_table(assetId)));
    auto latestHeight { stmt.one().process([](auto& row) {
        return Height(row[0]);
    }) };
    stmtSetLatestHeight.run(latestHeight.value_or(Height(0)), assetId);

    if (!latestHeight) { // no more trades present
        asset_drop_tables(assetId);
        return;
    } else { // still trades, we need to rebuild aggregate candles
        assert(latestHeight->is_zero() == false); // trades have nozero height
        auto begin { asset_erase_candles_from(assetId, nextHeight, timestampAtRollbackHeight) };
        asset_rebuild_candles(assetId, begin);
    }
}

void MarketDB::insert_asset(AssetId assetId, AssetHash hash)
{
    stmtInsertAsset.run(assetId, hash);
}

void MarketDB::insert_candle(AssetId assetId, Interval interval, const Candle& c)
{
    auto table { candles_table(assetId, interval) };
    Statement stmt(db, std::format("INSERT INTO {} (timestamp, height, open, high, low, close, base, quote) VALUES (?, ?, ?, ?, ?, ?, ?, ?)", table));
    stmt.run(c.timestamp, c.height, c.open, c.high, c.low, c.close, c.base, c.quote);
}

wrt::optional<Candle> MarketDB::get_latest_candle(AssetId aid, Interval interval) const
{
    auto tableName { candles_table(aid, interval) };
    std::string query { std::format("SELECT timestamp, height, open, high, low, close, base, quote FROM {} ORDER BY timestamp DESC LIMIT 1", tableName) };
    Statement stmt(db, query);
    return stmt.one().process([](auto& row) { return Candle {
                                                  .timestamp = row[0],
                                                  .height = row[1],
                                                  .open = row[2],
                                                  .high = row[3],
                                                  .low = row[4],
                                                  .close = row[5],
                                                  .base = row[6],
                                                  .quote = row[7],
                                              }; });
}

MarketDB::MarketDB(const std::string& path)
    : MarketReaderDB(create_database(path))
    , stmtSetLatestHeight(db, "UPDATE Assets SET LatestHeight = ? WHERE id = ?")
    , stmtInsertAsset(db, "INSERT INTO Assets (id, hash, latestHeight) VALUES (?, ?, 0)")
    , stmtInsertBlock(db, "INSERT INTO Blocks (height, hash, timestamp) VALUES(?,?, ?)")
    , stmtDeleteBlockFrom(db, "DELETE FROM BLOCKS WHERE height >= ?")
    , stmtSelectAssetsFrom(db, "SELECT id, latestHeight FROM Assets WHERE id >= ? ORDER BY id ASC")
    , stmtDeleteAssets(db, "DELETE FROM Assets WHERE id >= ?")
    , stmtSelectAssetIdsByLatestHeight(db, "SELECT id FROM ASSETS WHERE latestHeight >=?")
    , length(MarketReaderDB::chain_length())
{
}

void MarketDB::create_tables(AssetId aid)
{
    // create candles tables
    auto create_candles { [&](Interval interval) { db.exec(std::format("CREATE TABLE {} ( `timestamp` INTEGER NOT NULL, `height` INTEGER NOT NULL, `open` REAL NOT NULL, `high` REAL NOT NULL, `low` REAL NOT NULL, `close` REAL NOT NULL, `base` REAL NOT NULL, `quote` REAL NOT NULL, PRIMARY KEY(`timestamp`))", candles_table(aid, interval))); } };
    create_candles(FIVEMIN());
    create_candles(ONEHOUR());
    create_candles(ONEDAY());

    db.exec(std::format("CREATE TABLE {} (`height` INTEGER, `base` REAL NOT NULL, `quote` REAL NOT NULL,  PRIMARY KEY (`height`))", trades_table(aid)));
}

void MarketDB::asset_drop_tables(AssetId aid)
{
    auto drop_table { [&](std::string_view table) {
        db.exec(std::format("DROP TABLE IF EXISTS {}", table));
    } };
    auto drop_candles { [&](Interval interval) { drop_table(candles_table(aid, interval)); } };
    drop_candles(FIVEMIN());
    drop_candles(ONEHOUR());
    drop_candles(ONEDAY());
    drop_table(trades_table(aid));
}

void MarketDB::insert_block(NonzeroHeight height, BlockHash hash, Timestamp t)
{
    stmtInsertBlock.run(height, hash, t);
}

void MarketDB::delete_block_from(NonzeroHeight height)
{
    stmtDeleteBlockFrom.run(height);
}
wrt::optional<BlockHash> MarketDB::select_block(NonzeroHeight height)
{
    return stmtInsertBlock.one(height).process([](const sqlite::Row& row) {
        return BlockHash(row[0]);
    });
}

void MarketDB::delete_assets_from(AssetId assetId, NonzeroHeight fromHeight)
{
    stmtSelectAssetsFrom.for_each([&](auto& row) {
        AssetId assetId = row[0];
        Height latestHeight = row[1];
        if (!latestHeight.is_zero()) { // tables for this asset exist
            // There were trades inserted for this assetId.
            // Any trades must have happened after asset was created.
            assert(latestHeight >= fromHeight);

            asset_drop_tables(assetId);
        }
    },
        assetId);
    stmtDeleteAssets.run(assetId);
}
}

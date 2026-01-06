#include "chain_db.hpp"
#include "api/types/all.hpp"
#include "block/chain/header_chain.hpp"
#include "block/chain/history/history.hpp"
#include "block/header/header_impl.hpp"
#include "block/header/view_inline.hpp"
#include "chainserver/db/state_ids.hpp"
#include "db/sqlite.hpp"
#include "defi/token/account_token.hpp"
#include "defi/token/asset.hpp"
#include "defi/token/info.hpp"
#include "general/address_funds.hpp"
#include "general/hex.hpp"
#include "general/writer.hpp"
#include "global/globals.hpp"
#include "sqlite3.h"
#include "table_info.hpp"
#include "types.hpp"
#include "wrt/format.hpp"
#include <spdlog/spdlog.h>

using namespace std::string_literals;

namespace chain_db {

template <typename T, typename... R>
template <typename S>
Statement StateIdStatementsWrapper<StateIdBase<T, R...>>::delete_from_stmt(SQLite::Database& db)
{
    return { db, "DELETE FROM " + std::string(table_info::table_name<S>()) + " WHERE id >= ?" };
}

std::runtime_error ChainDB::database_error_for_type(std::string_view typeName)
{
    std::string msg { fmt_lib::format("Database does not contain {} for key", typeName) };
    return std::runtime_error(msg);
}

ChainDB::Database::Database(const std::string& path)
    : SQLite::Database([&]() -> auto& {
    spdlog::debug("Opening chain database \"{}\"", path);
    return path; }(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    exec("PRAGMA foreign_keys = ON");
    // exec("CREATE TABLE IF NOT EXISTS Metadata ("
    //      "id INTEGER NOT NULL  "
    //      "value BLOB, "
    //      "PRIMARY KEY(id))");
    exec("CREATE TABLE IF NOT EXISTS `" ACCOUNTHISTORY_TABLE "` ("
         "`account_id` INTEGER, "
         "`history_id` INTEGER, "
         "PRIMARY KEY(`account_id`,`history_id`)) "
         "WITHOUT ROWID");
    exec("CREATE INDEX IF NOT EXISTS `" ACCOUNTHISTORY_TABLE "Index` ON "
         "`" ACCOUNTHISTORY_TABLE "` (`history_id` ASC)");

    exec("CREATE TABLE IF NOT EXISTS `" BLOCKS_TABLE "` ( `height` INTEGER "
         "NOT NULL, `header` BLOB NOT NULL, `body` BLOB NOT NULL, "
         "`undo` BLOB DEFAULT NULL, `hash` BLOB NOT NULL UNIQUE )");

    // candles 5m
    exec("CREATE TABLE IF NOT EXISTS Candles5m ("
         "asset_id INTEGER NOT NULL, "
         "timestamp INTEGER NOT NULL, " // start timestamp
         "open INTEGER NOT NULL, "
         "high INTEGER NOT NULL, "
         "low INTEGER NOT NULL, "
         "close INTEGER NOT NULL, "
         "quantity INTEGER NOT NULL, " // base amount traded
         "volume INTEGER NOT NULL, " // quote amount traded
         "PRIMARY KEY(asset_id, timestamp))");
    exec("CREATE INDEX IF NOT EXISTS `Candles5mIndex` ON "
         "`Candles5m` (timestamp)");

    // candles 1h
    exec("CREATE TABLE IF NOT EXISTS Candles1h ("
         "asset_id INTEGER NOT NULL, "
         "timestamp INTEGER NOT NULL, " // start timestamp
         "open INTEGER NOT NULL, "
         "high INTEGER NOT NULL, "
         "low INTEGER NOT NULL, "
         "close INTEGER NOT NULL, "
         "quantity INTEGER NOT NULL, " // base amount traded
         "volume INTEGER NOT NULL, " // quote amount traded
         "PRIMARY KEY(asset_id, timestamp))");
    exec("CREATE INDEX IF NOT EXISTS `Candles1hIndex` ON "
         "`Candles1h` (timestamp)");

    // Sell orders
    exec("CREATE TABLE IF NOT EXISTS " SELLORDERS_TABLE " ("
         "id INTEGER NOT NULL, "
         "account_id INTEGER NOT NULL, "
         "pin_height INTEGER NOT NULL, "
         "nonce_id INTEGER NOT NULL, "
         "asset_id INTEGER NOT NULL, "
         "totalBase INTEGER NOT NULL, "
         "filledBase INTEGER NOT NULL, "
         "limitPrice INTEGER NOT NULL DEFAULT 0, "
         "PRIMARY KEY(`id`))");
    exec("CREATE INDEX IF NOT EXISTS `" SELLORDERS_TABLE "Index` ON "
         "`" SELLORDERS_TABLE "` (asset_id, limitPrice ASC, id ASC)");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS `" SELLORDERS_TABLE "AccountIndex` ON "
         "`" SELLORDERS_TABLE "` (account_id, pin_height, nonce_id)");

    // Buy orders
    exec("CREATE TABLE IF NOT EXISTS `" BUYORDERS_TABLE "` ("
         "id INTEGER NOT NULL, "
         "account_id INTEGER NOT NULL, "
         "pin_height INTEGER NOT NULL, "
         "nonce_id INTEGER NOT NULL, "
         "asset_id INTEGER NOT NULL, "
         "totalQuote INTEGER NOT NULL, "
         "filledQuote INTEGER NOT NULL, "
         "limitPrice INTEGER NOT NULL DEFAULT 0, "
         "PRIMARY KEY(`id`))");
    exec("CREATE INDEX IF NOT EXISTS `" BUYORDERS_TABLE "Index` ON "
         "`" BUYORDERS_TABLE "` (asset_id, limitPrice DESC, id ASC)");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS `" BUYORDERS_TABLE "AccountIndex` ON "
         "`" BUYORDERS_TABLE "` (account_id, pin_height, nonce_id)");

    exec("CREATE TABLE IF NOT EXISTS " CANCELED_TABLE " ("
         "id INTEGER NOT NULL, "
         "account_id INTEGER NOT NULL, "
         "pin_height INTEGER NOT NULL, "
         "nonce_id INTEGER NOT NULL, "
         "PRIMARY KEY(`id`))");
    exec("CREATE INDEX IF NOT EXISTS `" CANCELED_TABLE "Index` ON "
         "`" CANCELED_TABLE "` (account_id, pin_height, nonce_id)");

    // Pools
    exec("CREATE TABLE IF NOT EXISTS " POOLS_TABLE " ("
         "asset_id INTEGER NOT NULL, "
         "`liquidity_token` INTEGER NOT NULL, "
         "`liquidity_wart` INTEGER NOT NULL, "
         "`pool_shares` INTEGER NOT NULL, "
         "PRIMARY KEY(`asset_id`))");

    // Peg
    exec("CREATE TABLE IF NOT EXISTS " PEG_TABLE " ("
         "id INTEGER NOT NULL, "
         "account_id INTEGER NOT NULL, "
         "out_asset_id INTEGER NOT NULL, "
         "out_total INTEGER NOT NULL, "
         "out_paid INTEGER NOT NULL DEFAULT 0, "
         "in_total INTEGER NOT NULL, "
         "in_burnt INTEGER NOT NULL DEFAULT 0, "
         "in_asset_id INTEGER NOT NULL UNIQUE, "
         "expiration_height INTEGER NOT NULL, "
         "PRIMARY KEY(`id`))");
    exec("CREATE INDEX IF NOT EXISTS `" PEG_TABLE "Index` ON "
         "`" PEG_TABLE "` (expiration_height)");

    // TokenForkBalances
    exec("CREATE TABLE IF NOT EXISTS " TOKENFORKBALANCES_TABLE " ("
         "`id` INTEGER NOT NULL, "
         "`account_id` INTEGER NOT NULL, "
         "`asset_id` INTEGER NOT NULL, "
         "`height` INTEGER NOT NULL, "
         "`balance` INTEGER NOT NULL, "
         "PRIMARY KEY(`id`))");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS `" TOKENFORKBALANCES_TABLE "Index` "
         "ON `" TOKENFORKBALANCES_TABLE "` (account_id, asset_id, height)");

    // Assets
    exec("CREATE TABLE IF NOT EXISTS `" ASSETS_TABLE "` ( "
         "`id` INTEGER DEFAULT NULL, "
         "`hash` TEXT NOT NULL UNIQUE, "
         "`name` TEXT NOT NULL, "
         "`precision` INTEGER NOT NULL, "
         "`height` INTEGER NOT NULL, "
         "`owner_account_id` INTEGER NOT NULL, "
         "`total_supply` INTEGER NOT NULL, "
         "`group_id` INTEGER NOT NULL, " // group
         "`parent_id` INTEGER, " // used for forks
         "`data` BLOB,"
         "PRIMARY KEY(id))");
    exec("CREATE INDEX IF NOT EXISTS `" ASSETS_TABLE "Index` ON "
         "`" ASSETS_TABLE "` (height)");
    exec("CREATE INDEX IF NOT EXISTS `" ASSETS_TABLE "ParentIndex` ON "
         "`" ASSETS_TABLE "` (parent_id, height)");

    exec("CREATE TABLE IF NOT EXISTS `" BALANCES_TABLE "` (`id` INTEGER NOT NULL, `account_id` INTEGER NOT NULL, `token_id` INTEGER NOT NULL, `total` INTEGER NOT NULL DEFAULT 0, `locked` INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(`id`))");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS `" BALANCES_TABLE "Index` ON "
         "`" BALANCES_TABLE "` (`account_id` ASC, `token_id` ASC)");
    exec("CREATE INDEX IF NOT EXISTS `" BALANCES_TABLE "Index2` ON "
         "`" BALANCES_TABLE "` (`token_id`, `total` DESC)");
    exec("CREATE TABLE IF NOT EXISTS `" CONSENSUS_TABLE "` ( `height` INTEGER NOT "
         "NULL, `block_id` INTEGER NOT NULL, `history_cursor` INTEGER NOT "
         "NULL, `state_cursor` INTEGER NOT NULL, PRIMARY KEY(`height`) )");
    exec(" INSERT OR IGNORE INTO `" CONSENSUS_TABLE "` (`height`, "
         "`block_id`,`history_cursor`, `state_cursor`) "
         "VALUES "
         "(-1,x'"
         "0000000000000000000000000000000000000000000000000000000000"
         "000000',0,0);");
    exec("CREATE TABLE IF NOT EXISTS `" ACCOUNTS_TABLE "` ( `id` INTEGER NOT NULL, "
         "`address` BLOB NOT NULL UNIQUE, PRIMARY KEY(`id`))");
    exec("CREATE INDEX IF NOT EXISTS `" ACCOUNTS_TABLE "Index` ON `" ACCOUNTS_TABLE "` ( `id`)");
    exec("CREATE TABLE IF NOT EXISTS `" BADBLOCKS_TABLE "` ( `height` INTEGER, "
         "`header` "
         "BLOB UNIQUE )");
    exec("CREATE TABLE IF NOT EXISTS `" DELETESCHEDULE_TABLE "` ( `block_id`	INTEGER NOT NULL, `deletion_key`	INTEGER, PRIMARY KEY(`block_id`))");

    // create indices
    exec("CREATE INDEX IF NOT EXISTS `deletion_key` ON `" DELETESCHEDULE_TABLE "` ( `deletion_key`)");

    exec("CREATE TABLE IF NOT EXISTS `" FILLLINK_TABLE "` ( "
         "`id` INTEGER NOT NULL, " // historyId of fills
         "`link` INTEGER NOT NULL, " // history id of order creation
         " PRIMARY KEY(`id`))");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS `" FILLLINK_TABLE "Index` ON "
         "`" FILLLINK_TABLE "` (link, id)");

    exec("CREATE TABLE IF NOT EXISTS `" HISTORY_TABLE "` ( `id` INTEGER NOT NULL, "
         "`hash` BLOB NOT NULL, `data` BLOB NOT NULL, PRIMARY KEY(`id`))");
    exec("CREATE INDEX IF NOT EXISTS `" HISTORY_TABLE "Index` ON "
         "`" HISTORY_TABLE "` (`hash` ASC)");
}
namespace {
struct GetNextId {
    SQLite::Database& db;
    template <typename T>
    operator T() const
    {
        auto get_int64 {
            [&](const std::string& s) { return int64_t(db.execAndGet(s).getInt64()); }
        };
        auto next_id64 {
            [&](std::string s) {
                return get_int64("SELECT coalesce(max(ROWID),0) + 1 FROM `" + s + "`");
            }
        };
        return T(next_id64(table_info::table_name<T>()));
    }
};

}
ChainDB::Cache ChainDB::Cache::init(SQLite::Database& db)
{
    GetNextId nextId(db);
    return {
        .ids { /* StateId32::max_component(nextId), */ StateId64::max_component(nextId) },
        .nextHistoryId { static_cast<HistoryId>(nextId) },
        .deletionKey { 2 }
    };
}

ChainDBTransaction ChainDB::transaction()
{
    return ChainDBTransaction(*this);
}
ChainDB::ChainDB(const std::string& path)
    : fl(path)
    , db(path)
    , cache(Cache::init(db))
    // , state32Statements(db)
    , state64Statements(db)
    , stmtBlockInsert(db, "INSERT INTO `" BLOCKS_TABLE "` ( `height`, `header`, `body` "
                          ", `hash`) VALUES (?,?,?,?)")
    , stmtUndoSet(db, "UPDATE `" BLOCKS_TABLE "` SET `undo`=? WHERE `ROWID`=?")
    , stmtBlockGetUndo(
          db, "SELECT `header`,`body`, `undo` FROM `" BLOCKS_TABLE "` WHERE `ROWID`=?")
    , stmtBlockById(
          db, "SELECT `height`, `header`, `body` FROM `" BLOCKS_TABLE "` WHERE `ROWID`=?;")
    , stmtBlockByHash(
          db, "SELECT ROWID, `height`, `header`, `body` FROM `" BLOCKS_TABLE "` WHERE `hash`=?;")
    , stmtPruneCandles5m(db, "DELETE FROM Candles5m WHERE timestamp >=?")
    , stmtInsertCandles5m(db, "INSERT OR REPLACE INTO Candles5m (asset_id, timestamp, open, high, low, close, quantity, volume) VALUES (?,?,?,?,?,?,?,?)")
    , stmtSelectCandles5m(db, "SELECT timestamp, open, high, low, close, quantity, volume FROM Candles5m WHERE asset_id=? AND timestamp>=? AND timestamp<=?")
    , stmtPruneCandles1h(db, "DELETE FROM Candles1h WHERE timestamp >=?")
    , stmtInsertCandles1h(db, "INSERT OR REPLACE INTO Candles1h (asset_id, timestamp, open, high, low, close, quantity, volume) VALUES (?,?,?,?,?,?,?,?)")
    , stmtSelectCandles1h(db, "SELECT timestamp, open, high, low, close, quantity, volume FROM Candles1h WHERE asset_id=? AND timestamp>=? AND timestamp<=?")

    , stmtInsertBaseSellOrder(db, "INSERT INTO `" SELLORDERS_TABLE "` (id, account_id, pin_height, nonce_id, asset_id, totalBase, filledBase, limitPrice) VALUES(?,?,?,?,?,?,?,?)")
    , stmtUpdateFillBaseSellOrder(db, "UPDATE `" SELLORDERS_TABLE "` SET filledBase = ? WHERE id = ?")
    , stmtDeleteBaseSellOrder(db, "DELETE FROM `" SELLORDERS_TABLE "` WHERE id = ?")
    , stmtDeleteBaseSellOrderTxid(db, "DELETE FROM `" SELLORDERS_TABLE "` WHERE account_id = ? AND pin_height = ? AND nonce_id = ?")
    , stmtInsertQuoteBuyOrder(db, "INSERT INTO `" BUYORDERS_TABLE "` (id, account_id, pin_height, nonce_id, asset_id, totalQuote, filledQuote, limitPrice) VALUES(?,?,?,?,?,?,?,?)")
    , stmtUpdateFillQuoteBuyOrder(db, "UPDATE `" BUYORDERS_TABLE "` SET filledQuote = ? WHERE id = ?")
    , stmtDeleteQuoteBuyOrder(db, "DELETE FROM `" BUYORDERS_TABLE "` WHERE id = ?")
    , stmtDeleteQuoteBuyOrderTxid(db, "DELETE FROM `" BUYORDERS_TABLE "` WHERE account_id = ? AND pin_height = ? AND nonce_id = ?")
    , stmtSelectBaseSellOrderAsc(db, "SELECT id, account_id, pin_height, nonce_id, totalBase, filledBase, limitPrice FROM `" SELLORDERS_TABLE "` WHERE asset_id=? ORDER BY limitPrice ASC, id ASC")
    , stmtSelectQuoteBuyOrderDesc(db, "SELECT id, account_id, pin_height, nonce_id, totalQuote, filledQuote, limitPrice FROM `" BUYORDERS_TABLE "` WHERE asset_id=? ORDER BY limitPrice DESC, id ASC")
    , stmtSelectBaseSell(db, "SELECT id, asset_id, totalBase, filledBase, limitPrice FROM `" SELLORDERS_TABLE "` WHERE account_id = ? AND pin_height = ? AND nonce_id = ?")
    , stmtSelectQuoteBuy(db, "SELECT id, asset_id, totalQuote, filledQuote, limitPrice FROM `" BUYORDERS_TABLE "` WHERE account_id = ? AND pin_height = ? AND nonce_id = ?")
    , stmtInsertCanceled(db, "INSERT INTO `" CANCELED_TABLE "` (id, account_id, pin_height, nonce_id) VALUES (?,?,?,?)")
    , stmtDeleteCanceled(db, "DELETE FROM `" CANCELED_TABLE "` WHERE id = ?")
    , stmtInsertPool(db, "INSERT INTO `" POOLS_TABLE "` (asset_id, liquidity_wart, liquidity_token, pool_shares) VALUES (?,?,?,?)")
    , stmtDeletePool(db, "DELETE FROM `" POOLS_TABLE "` WHERE asset_id = ?")
    , stmtSelectPool(db, "SELECT asset_id, liquidity_token, liquidity_wart, pool_shares FROM `" POOLS_TABLE "` WHERE asset_id=?")
    , stmtUpdatePool(db, "UPDATE `" POOLS_TABLE "` SET liquidity_token=?, liquidity_wart=?, pool_shares=? WHERE asset_id=?")
    , stmtTokenForkBalanceInsert(db, "INSERT INTO `" TOKENFORKBALANCES_TABLE "` "
                                     "(id, account_id, asset_id, height, balance) "
                                     "VALUES (?,?,?,?,?)")
    , stmtTokenForkBalanceEntryExists(db, "SELECT 1 FROM `" TOKENFORKBALANCES_TABLE "` "
                                          "WHERE account_id=? "
                                          "AND asset_id=? "
                                          "AND height=?")
    , stmtTokenForkBalanceSelect(db, "SELECT height, balance FROM `" TOKENFORKBALANCES_TABLE "` WHERE account_id=? AND height>=? ORDER BY height ASC LIMIT 1")

    , stmtAssetInsert(db, "INSERT INTO `" ASSETS_TABLE "` (id, hash, name, precision, height, owner_account_id, total_supply, group_id, parent_id, data) VALUES (?,?,?,?,?,?,?,?,?,?)")
    , stmtAssetSelectForkHeight(db, "SELECT height FROM `" ASSETS_TABLE "` WHERE parent_id=? AND height>=? ORDER BY height DESC LIMIT 1")
    , stmtAssetLookup(db, "SELECT id, hash, name, precision, height, owner_account_id, total_supply, group_id, parent_id FROM `" ASSETS_TABLE "` WHERE `id`=?")
    , stmtAssetLookupByPrefix(db, "SELECT id, hash, name, precision, height, owner_account_id, total_supply, group_id, parent_id FROM `" ASSETS_TABLE "` WHERE `name` LIKE ? LIMIT 100")
    , stmtAssetLookupByHash(db, "SELECT id, hash, name, precision, height, owner_account_id, total_supply, group_id, parent_id FROM `" ASSETS_TABLE "` WHERE `hash`=?")
    , stmtSelectBalanceId(db, "SELECT `account_id`, `token_id`, `total`, `locked` FROM `" BALANCES_TABLE "` WHERE `id`=?")
    , stmtTokenInsertBalance(db, "INSERT INTO `" BALANCES_TABLE "` ( id, `token_id`, `account_id`, `total`, `locked`) VALUES (?,?,?,?,?)")
    , stmtTokenSelectBalance(db, "SELECT `id`, `total`, `locked` FROM `" BALANCES_TABLE "` WHERE `token_id`=? AND `account_id`=?")
    , stmtAccountSelectAssets(db, "SELECT `token_id`, `total`, `locked` FROM `" BALANCES_TABLE "` WHERE `account_id`=? LIMIT ?")
    , stmtTokenUpdateBalanceTotalById(db, "UPDATE `" BALANCES_TABLE "` SET `total`=?, `locked`=? WHERE `id`=?")
    , stmtTokenSelectRichlist(db, "SELECT `address`, `total` FROM `" BALANCES_TABLE "` b JOIN `" ACCOUNTS_TABLE "` a on a.id = b.account_id WHERE `token_id`=? ORDER BY `total` DESC LIMIT ?")
    , stmtConsensusHeaders(db, "SELECT c.height, c.history_cursor, c.state_cursor, b.header "
                               "FROM `" BLOCKS_TABLE "` b JOIN `" CONSENSUS_TABLE "` c ON "
                               "b.ROWID=c.block_id ORDER BY c.height ASC;")
    , stmtConsensusInsert(db, "INSERT INTO `" CONSENSUS_TABLE "` ( `height`, "
                              "`block_id`, `history_cursor`, `state_cursor`) VALUES (?,?,?,?)")
    , stmtConsensusSetProperty(
          db, "INSERT OR REPLACE INTO `" CONSENSUS_TABLE "` (`height`,`block_id`, `history_cursor`, `state_cursor`) VALUES (?,?,0,0)")
    , stmtConsensusSelect(
          db, "SELECT `block_id` FROM `" CONSENSUS_TABLE "` WHERE `height`=?")
    , stmtConsensusSelectRange(
          db, "SELECT `block_id` FROM `" CONSENSUS_TABLE "` WHERE `height`>=? AND "
              "`height` <? ORDER BY height ASC")
    , stmtConsensusSelectHistory(
          db, "SELECT `history_cursor` FROM `" CONSENSUS_TABLE "` WHERE `height`=?")
    , stmtConsensusHead(
          db, "SELECT `height`, `block_id` FROM `" CONSENSUS_TABLE "` ORDER BY "
              "`height` DESC LIMIT 1;")
    , stmtConsensusDeleteFrom(db, "DELETE FROM `" CONSENSUS_TABLE "` WHERE `height`>=?")

    , stmtScheduleExists(db, "SELECT EXISTS(SELECT 1 FROM `" DELETESCHEDULE_TABLE "` WHERE `block_id`=?)")
    , stmtScheduleInsert(db, "INSERT INTO `" DELETESCHEDULE_TABLE "` (`block_id`,`deletion_key`) VALUES (?,?)")
    , stmtScheduleBlock(db, "UPDATE `" DELETESCHEDULE_TABLE "` SET `deletion_key`=? WHERE `block_id` = ?")
    , stmtScheduleProtected(db, "UPDATE `" DELETESCHEDULE_TABLE "` SET `deletion_key`=? WHERE `deletion_key` = 0")
    , stmtScheduleDelete(db, "DELETE FROM `" DELETESCHEDULE_TABLE "` WHERE `block_id` = ?")
    , stmtScheduleConsensus(db, "REPLACE INTO `" DELETESCHEDULE_TABLE "` (block_id,deletion_key) SELECT block_id, ? FROM `" CONSENSUS_TABLE "` WHERE height >= ?")

    , stmtDeleteGCBlocks(
          db, "DELETE FROM `" BLOCKS_TABLE "` WHERE ROWID IN (SELECT `block_id`  FROM "
              "`" DELETESCHEDULE_TABLE "` WHERE `deletion_key`<=? AND `deletion_key` > 0 )")
    , stmtDeleteGCRefs(db, "DELETE  FROM `" DELETESCHEDULE_TABLE "` WHERE `deletion_key`<=? AND `deletion_key` > 0")

    , stmtAccountsInsert(db, "INSERT INTO `" ACCOUNTS_TABLE "` ( `id`, `address`"
                             ") VALUES (?,?)")

    , stmtBadblockInsert(
          db, "INSERT INTO `" BADBLOCKS_TABLE "` (`height`, `header`) VALUES (?,?)")
    , stmtBadblockGet(db, "SELECT `height`, `header` FROM `" BADBLOCKS_TABLE "`")

    , stmtAccountsLookup(db, "SELECT `Address` FROM `" ACCOUNTS_TABLE "` WHERE id=?")
    , stmtHistoryLinkInsert(db, "INSERT INTO `" FILLLINK_TABLE "` (id,link) VALUES (?,?)")
    , stmtHistoryInsert(db, "INSERT INTO `" HISTORY_TABLE "` (`id`,`hash`, `data`"
                            ") VALUES (?,?,?)")
    , stmtHistoryDeleteFrom(db, "DELETE FROM `" HISTORY_TABLE "` WHERE `id`>=?")
    , stmtHistoryLookup(db,
          "SELECT `id`, `data` FROM `" HISTORY_TABLE "` WHERE `hash`=?")
    , stmtHistoryLookupRange(db, "SELECT `id`, `hash`, `data` FROM `" HISTORY_TABLE "` WHERE `id`>=? AND `id`<?")
    , stmtAccountHistoryInsert(db, "INSERT OR IGNORE INTO `" ACCOUNTHISTORY_TABLE "` "
                                   "(`account_id`,`history_id`) VALUES (?,?)")
    , stmtAccountHistoryDeleteFrom(
          db, "DELETE FROM `" ACCOUNTHISTORY_TABLE "` WHERE `history_id`>=?")
    , stmtBlockIdSelect(
          db, "SELECT `ROWID` FROM `" BLOCKS_TABLE "` WHERE `hash`=?")
    , stmtBlockHeightSelect(
          db, "SELECT `height` FROM `" BLOCKS_TABLE "` WHERE `hash`=?")
    , stmtBlockDelete(db, "DELETE FROM `" BLOCKS_TABLE "` WHERE ROWID = ?")

    // BELOW STATEMENTS REQUIRED FOR INDEXING NODES
    //
    , stmtAddressLookup(db, "SELECT ROWID FROM " ACCOUNTS_TABLE " WHERE `address`=?")
    , stmtHistoryById(db, "SELECT h.id, `hash`,`data` FROM `" HISTORY_TABLE "` `h` JOIN "
                          "`" ACCOUNTHISTORY_TABLE "` `ah` ON h.id=`ah`.history_id WHERE "
                          "ah.`account_id`=? AND h.id<? ORDER BY h.id DESC LIMIT 100")
    , stmtGetDBSize(db, "SELECT page_count * page_size FROM pragma_page_count(), pragma_page_size();")
{
    //
    // Do DELETESCHEDULE cleanup
    db.exec("UPDATE `" DELETESCHEDULE_TABLE "` SET `deletion_key`=1");
}

void ChainDB::insert_unguarded(const AccountData& ad)
{
    stmtAccountsInsert.run(ad.id, ad.address);
}

// void ChainDB::delete_state32_from(StateId32 fromId)
// {
//     assert(fromId.value() > 0);
//     auto& next_ref { cache.ids.next32 };
//     if (next_ref <= fromId) {
//         spdlog::error("BUG: Deleting nothing, fromId = {} >= {} = cache.stateId32", fromId.value(), next_ref.value());
//     } else {
//         next_ref = fromId;
//         state32Statements.delete_from(fromId);
//     }
// }
void ChainDB::delete_state64_from(StateId64 fromId)
{
    assert(fromId.value() > 0);
    auto& next_ref { cache.ids.next64 };
    if (next_ref < fromId) {
        spdlog::error("BUG: Deleting nothing, fromId = {} >= {} = cache.stateId64", fromId.value(), next_ref.value());
    } else {
        next_ref = fromId;
        state64Statements.delete_from(fromId);
    }
}

void ChainDB::insert_consensus(NonzeroHeight height, BlockId blockId, HistoryId historyCursor, StateId64 stateId)
{
    stmtConsensusInsert.run(height, blockId, historyCursor, stateId);
    stmtScheduleDelete.run(blockId);
}

Worksum ChainDB::get_consensus_work() const
{
    auto o { stmtConsensusSelect.one(WORKSUMID) };
    if (!o.has_value()) {
        throw std::runtime_error("Database corrupted. No worksum entry");
    }
    return o[0];
}
void ChainDB::set_consensus_work(const Worksum& ws)
{
    stmtConsensusSetProperty.run(WORKSUMID, ws);
}

wrt::optional<SignedSnapshot> ChainDB::get_signed_snapshot() const
{
    auto o { stmtConsensusSelect.one(SIGNEDPINID) };
    if (!o.has_value()) {
        return {};
    }
    auto v(o.get_vector(0));
    Reader r(v);
    try {
        return SignedSnapshot(r);
    } catch (Error e) {
        throw std::runtime_error("Database corrupted. Signed snapshot invalid: "s + std::string(e.strerror()));
    }
}

void ChainDB::set_signed_snapshot(const SignedSnapshot& ss)
{
    std::vector<uint8_t> v(SignedSnapshot::binary_size);
    Writer w(v);
    w << ss;
    stmtConsensusSetProperty.run(SIGNEDPINID, v);
}

std::vector<BlockId> ChainDB::consensus_block_ids(HeightRange range) const
{
    auto out { stmtConsensusSelectRange.all([&](const sqlite::Row& r) {
        return BlockId { r[0] };
    },
        range.hbegin, range.hend) };
    if (out.size() != range.hend - range.hbegin)
        throw std::runtime_error("Cannot find block ids in database: " + std::to_string(range.hbegin.value()) + "-" + std::to_string(range.hend.value()));
    return out;
}

wrt::optional<BlockId> ChainDB::consensus_block_id(Height h) const
{
    return stmtConsensusSelectRange.one(h, h.add1()).process([&](const sqlite::Row& r) {
        return BlockId { r[0] };
    });
}

void ChainDB::garbage_collect_blocks(DeletionKey dk)
{
    stmtDeleteGCBlocks.run(dk.value());
    stmtDeleteGCRefs.run(dk.value());
}

DeletionKey ChainDB::schedule_protected_all()
{
    auto dk { cache.deletionKey++ };
    stmtScheduleProtected.run(dk.value());
    return dk;
}

bool ChainDB::schedule_exists(BlockId id)
{
    return stmtScheduleExists.one(id.value()).get<int64_t>(0) != 0;
}
bool ChainDB::consensus_exists(Height h, BlockId dk)
{
    return stmtConsensusSelect.one(h).get<uint64_t>(0) == dk.value();
}

DeletionKey ChainDB::schedule_protected_part(Headerchain hc, NonzeroHeight fromHeight)
{
    auto dk { cache.deletionKey++ };
    for (auto h { fromHeight }; h < hc.length(); ++h) {
        auto id { lookup_block_id(hc.get_hash(h).value()) };
        assert(stmtScheduleBlock.run(dk.value(), id.value()) == 1);
    }
    stmtScheduleProtected.run(dk.value());
    return dk;
}

void ChainDB::protect_stage_assert_scheduled(BlockId id)
{
    assert(schedule_exists(id));
    assert(stmtScheduleBlock.run(0, id) == 1);
}

DeletionKey ChainDB::delete_consensus_from(NonzeroHeight height)
{
    auto dk { cache.deletionKey++ };
    stmtScheduleConsensus.run(dk.value(), height);
    stmtConsensusDeleteFrom.run(height);
    return dk;
}

wrt::optional<Block> ChainDB::get_block(BlockId id) const
{
    return get_block_data(id).and_then([&](BlockData&& bd) -> wrt::optional<Block> { return std::move(bd).parse_throw(); });
}

wrt::optional<BlockData> ChainDB::get_block_data(BlockId id) const
{
    auto o { stmtBlockById.one(id) };
    if (!o.has_value())
        return {};
    try {
        return BlockData(o[0], Header(o[1]), BodyData(o[2]));
    } catch (...) {
        throw std::runtime_error("Cannot load block with id " + std::to_string(id.value()) + ". ");
    }
}

wrt::optional<BodyData> ChainDB::get_block_body(HashView hash) const
{
    auto o = stmtBlockByHash.one(hash);
    if (!o.has_value())
        return {};
    try {
        return BodyData(std::vector<uint8_t>(o[3]));
    } catch (...) {
        throw std::runtime_error("Cannot load block with hash " + serialize_hex(hash) + ".");
    }
}

wrt::optional<std::pair<BlockId, Block>> ChainDB::get_block(HashView hash) const
{
    auto o = stmtBlockByHash.one(hash);
    if (!o.has_value())
        return {};
    try {
        return std::pair<BlockId, Block> {
            BlockId(o[0]),
            BlockData { o[1], Header(o[2]), BodyData(o[3]) }.parse_throw()
        };
    } catch (...) {
        throw std::runtime_error("Cannot load block with hash " + serialize_hex(hash) + ".");
    }
}

std::pair<BlockId, bool> ChainDB::insert_protect(const Block& b)
{
    auto hash { b.header.hash() };

    auto blockId { lookup_block_id(hash) };
    if (blockId.has_value()) {
        assert(schedule_exists(*blockId) || consensus_exists(b.height, *blockId));
        return { blockId.value(), false };
    } else {
        stmtBlockInsert.run(b.height, b.header, b.body.data, hash);
        auto lastId { db.getLastInsertRowid() };
        stmtScheduleInsert.run(lastId, 0);
        return { BlockId(lastId), true };
    }
}

wrt::optional<BlockUndoData>
ChainDB::get_block_undo(BlockId id) const
{
    return stmtBlockGetUndo.one(id).process([](auto& a) {
        {
            return BlockUndoData {
                .header = a[0],
                .body = a.get_vector(1),
                .rawUndo = { a.get_vector(2) }
            };
        }
    });
}

void ChainDB::set_block_undo(BlockId id, const std::vector<uint8_t>& undo)
{
    stmtUndoSet.run(undo, id);
}

void ChainDB::prune_candles(Timestamp timestamp)
{
    stmtPruneCandles5m.run(timestamp);
    stmtPruneCandles1h.run(timestamp);
}

void ChainDB::insert_candles_5m(TokenId tid, const Candle& c)
{
    stmtInsertCandles5m.run(tid, c.timestamp, c.open, c.high, c.low, c.close, c.quantity, c.volume);
}

wrt::optional<Candle> ChainDB::select_candle_5m(TokenId tid, Timestamp ts)
{
    return stmtSelectCandles5m.one(tid, ts, ts).process([](auto& o) {
        return Candle {
            .timestamp = static_cast<int64_t>(o[0]),
            .open = o[1],
            .high = o[2],
            .low = o[3],
            .close = o[4],
            .quantity = o[5],
            .volume = o[6],
        };
    });
}

std::vector<Candle> ChainDB::select_candles_5m(TokenId tid, Timestamp from, Timestamp to)
{
    return stmtSelectCandles5m.all(
        [](const auto& o) {
            return Candle {
                .timestamp = static_cast<int64_t>(o[0]),
                .open = o[1],
                .high = o[2],
                .low = o[3],
                .close = o[4],
                .quantity = o[5],
                .volume = o[6],
            };
        },
        tid, from, to);
}

void ChainDB::insert_candles_1h(TokenId tid, const Candle& c)
{
    stmtInsertCandles1h.run(tid, c.timestamp, c.open, c.high, c.low, c.close, c.quantity, c.volume);
}

wrt::optional<Candle> ChainDB::select_candle_1h(TokenId tid, Timestamp ts)
{
    return stmtSelectCandles1h.one(tid, ts, ts).process([](auto& o) {
        return Candle {
            .timestamp = static_cast<int64_t>(o[0]),
            .open = o[1],
            .high = o[2],
            .low = o[3],
            .close = o[4],
            .quantity = o[5],
            .volume = o[6],
        };
    });
}

std::vector<Candle> ChainDB::select_candles_1h(TokenId tid, Timestamp from, Timestamp to)
{
    return stmtSelectCandles1h.all([](const auto& o) {
        return Candle {
            .timestamp = static_cast<int64_t>(o[0]),
            .open = o[1],
            .high = o[2],
            .low = o[3],
            .close = o[4],
            .quantity = o[5],
            .volume = o[6],
        };
    },
        tid, from, to);
}

void ChainDB::insert(const chain_db::OrderData& o)
{
    if (o.buy)
        stmtInsertQuoteBuyOrder.run(o.id, o.txid.accountId, o.txid.pinHeight, o.txid.nonceId, o.aid, o.total, o.filled, o.limit);
    else
        stmtInsertBaseSellOrder.run(o.id, o.txid.accountId, o.txid.pinHeight, o.txid.nonceId, o.aid, o.total, o.filled, o.limit);
}

void ChainDB::update_order_fillstate(const chain_db::OrderFillstate& o)
{
    if (o.buy)
        stmtUpdateFillQuoteBuyOrder.run(o.filled, o.id);
    else
        stmtUpdateFillBaseSellOrder.run(o.filled, o.id);
}

void ChainDB::delete_order(const chain_db::OrderDelete& od)
{
    if (od.buy)
        stmtDeleteQuoteBuyOrder.run(od.id);
    else
        stmtDeleteBaseSellOrder.run(od.id);
}

wrt::optional<chain_db::OrderData> ChainDB::select_order(TransactionId id) const
{
    using ret_t = chain_db::OrderData;

    wrt::optional<chain_db::OrderData> res {
        stmtSelectBaseSell.one(id.accountId, id.pinHeight, id.nonceId).process([&](const sqlite::Row& o) {
            return ret_t {
                .id = o[0],
                .buy = false,
                .txid { id },
                .aid = o[1],
                .total = o[2],
                .filled = o[3],
                .limit = o[4]
            };
        })
    };
    if (!res) {
        res = stmtSelectQuoteBuy.one(id.accountId, id.pinHeight, id.nonceId).process([&](auto o) {
            return ret_t {
                .id = o[0],
                .buy = true,
                .txid = id,
                .aid = o[1],
                .total = o[2],
                .filled = o[3],
                .limit = o[4]
            };
        });
    }
    return res;
}
OrderLoaderAscending ChainDB::base_order_loader_ascending(AssetId aid) const
{
    return { stmtSelectBaseSellOrderAsc.bind_multiple(aid) };
}

OrderLoaderDescending ChainDB::quote_order_loader_descending(AssetId aid) const
{
    return { stmtSelectQuoteBuyOrderDesc.bind_multiple(aid) };
}

void ChainDB::insert_canceled(CancelId cid, AccountId aid, PinHeight ph, NonceId nid)
{
    stmtInsertCanceled.run(cid, aid, ph, nid);
}
void ChainDB::delete_canceled(CancelId cid)
{
    stmtDeleteCanceled.run(cid);
}

void ChainDB::insert_pool(const PoolData& d)
{
    stmtInsertPool.run(d.asset_id(), d.quote, d.base, d.shares_total());
}

void ChainDB::delete_pool(AssetId id)
{
    stmtDeletePool.run(id);
}

wrt::optional<PoolData> ChainDB::select_pool(AssetId assetId) const
{
    return stmtSelectPool.one(assetId).process([](auto o) -> PoolData {
        return PoolData {
            { .assetId = o[0],
                .base = o[1],
                .quote = o[2],
                .shares = o[3] }
        };
    });
}

void ChainDB::update_pool(const PoolData& d)
{
    stmtUpdatePool.run(d.base, d.quote, d.shares_total(), d.asset_id());
}

void ChainDB::insert_unguarded(const TokenForkBalanceData& b)
{
    stmtTokenForkBalanceInsert.run(b.id, b.accountId, b.assetId, b.height, b.balance);
}

// bool ChainDB::fork_balance_exists(AccountToken at, NonzeroHeight h)
// {
//     return stmtTokenForkBalanceEntryExists.one(at.account_id(), at.asset_id(), h)
//         .process([](auto& o) {
//             return o.has_value();
//         });
// }

wrt::optional<std::pair<NonzeroHeight, Funds_uint64>> ChainDB::get_balance_snapshot_after(TokenId tokenId, NonzeroHeight minHegiht) const
{
    auto res { stmtTokenForkBalanceSelect.one(tokenId, minHegiht) };
    if (!res.has_value())
        return {};
    return std::pair<NonzeroHeight, Funds_uint64> { res[0], res[1] };
}

void ChainDB::insert_unguarded(const AssetData& d)
{
    stmtAssetInsert.run(d.id, d.hash, d.name, d.supply.precision.value(), d.height, d.ownerAccountId, d.supply.funds.value(), d.groupId, d.parentId, d.data);
}

wrt::optional<NonzeroHeight> ChainDB::get_latest_fork_height(TokenId tid, Height h)
{
    auto res { stmtAssetSelectForkHeight.one(tid, h) };
    if (!res.has_value())
        return {};
    return NonzeroHeight { res[0] };
}

void ChainDB::insert_unguarded(const BalanceData& b)
{
    stmtTokenInsertBalance.run(b.id, b.tokenId, b.accountId, b.total, b.locked);
}

wrt::optional<std::pair<BalanceId, Balance_uint64>> ChainDB::get_balance(AccountId aid, TokenId tid) const
{
    auto res { stmtTokenSelectBalance.one(tid, aid) };
    if (!res.has_value())
        return {};
    return std::pair<BalanceId, Balance_uint64> { res[0], Balance_uint64::from_total_locked(res[1], res[2]) };
}

std::vector<std::pair<TokenId, Funds_uint64>> ChainDB::get_tokens(AccountId accountId, size_t limit)
{
    return stmtAccountSelectAssets.all([&](const sqlite::Row& r) {
        return std::pair { TokenId { r[0] }, Funds_uint64 { r[1] } };
    },
        accountId, limit);
}

void ChainDB::set_balance(BalanceId id, Balance_uint64 bl)
{
    stmtTokenUpdateBalanceTotalById.run(bl.total, bl.locked, id);
}

api::Richlist ChainDB::lookup_richlist(TokenId tokenId, size_t limit) const
{
    api::Richlist out;
    stmtTokenSelectRichlist.for_each([&](sqlite::Row& r) {
        out.entries.push_back({ r[0], r[1] });
    },
        tokenId, limit);
    return out;
}

std::tuple<std::vector<Batch>, HistoryHeights, State64Heights> ChainDB::get_consensus_headers() const
{
    uint32_t h = 1;
    std::vector<Batch> batches;
    HistoryHeights historyHeights;
    State64Heights accountHeights;
    Batch b;
    stmtConsensusHeaders.for_each([&](sqlite::Row& r) {
        if (h != r.get<Height>(0)) { // corrupted
            throw std::runtime_error("Database corrupted, block height not consecutive");
        }
        historyHeights.append(r.get<HistoryId>(1));
        accountHeights.append(state_id(r.get<AccountId>(2)));
        Header header { r.get_array<80>(3) };
        if (b.size() >= HEADERBATCHSIZE) {
            assert(b.complete());
            batches.push_back(std::move(b));
            b.clear();
        }
        b.append(header);
        h += 1;
    });
    if (b.size() > 0) {
        batches.push_back(std::move(b));
    }
    return { std::move(batches), std::move(historyHeights), std::move(accountHeights) };
}

void ChainDB::insert_bad_block(NonzeroHeight height,
    const HeaderView header)
{
    stmtBadblockInsert.run(height, header);
}

std::vector<std::pair<Height, Header>>
ChainDB::getBadblocks() const
{
    return stmtBadblockGet.all([&](const sqlite::Row& r) {
        return std::pair<Height, Header> { r[0], r[1] };
    });
}

void ChainDB::insert_history_link(HistoryId parent, HistoryId link)
{
    stmtHistoryLinkInsert.run(parent, link);
}

HistoryId ChainDB::insertHistory(const HashView hash,
    const std::vector<uint8_t>& data)
{
    stmtHistoryInsert.run(cache.nextHistoryId, hash, data);
    return cache.nextHistoryId++;
}

void ChainDB::delete_history_from(NonzeroHeight h)
{
    const auto nextHistoryId { stmtConsensusSelectHistory.one(h).get<uint64_t>(0) };
    stmtHistoryDeleteFrom.run(nextHistoryId);
    stmtAccountHistoryDeleteFrom.run(nextHistoryId);
    cache.nextHistoryId = HistoryId { nextHistoryId };
}

wrt::optional<std::pair<history::HistoryVariant, HistoryId>> ChainDB::lookup_history(const HashView hash) const
{
    return stmtHistoryLookup.one(hash).process([](auto& o) {
        return std::pair<history::HistoryVariant, HistoryId> {
            ReadExhaustive(std::vector<uint8_t>(o[1])), o[0]
        };
    });
}

size_t ChainDB::byte_size() const
{
    return stmtGetDBSize.one().get<int64_t>(0);
}

std::vector<std::pair<HistoryId, history::Entry>> ChainDB::lookup_history_range(HistoryId lower, HistoryId upper) const
{
    int64_t l = lower.value();
    int64_t u = (upper == HistoryId { 0 } ? std::numeric_limits<int64_t>::max() : upper.value());
    try {
        return stmtHistoryLookupRange.all([&](const sqlite::Row& r) {
            return std::pair<HistoryId, history::Entry> { r[0], { r[1], history::HistoryVariant(ReadExhaustive(r.get_vector(2))) } };
        },
            l, u);
    } catch (...) {
        spdlog::error("Cannot load history entries [{},{}]", lower.value(), upper.value());
        throw;
    }
}

wrt::optional<history::Entry> ChainDB::lookup_history(HistoryId id) const
{
    auto v { lookup_history_range(id, id + 1) };
    if (v.size() == 0)
        return wrt::nullopt;
    return std::move(v.front().second);
}

void ChainDB::insertAccountHistory(AccountId accountId, HistoryId historyId)
{
    stmtAccountHistoryInsert.run(accountId, historyId);
}

wrt::optional<AccountId> ChainDB::lookup_account(const AddressView address) const
{
    return stmtAddressLookup.one(address).process([](auto& p) {
        return AccountId { p[0] };
    });
}

std::vector<std::tuple<HistoryId, history::Entry>> ChainDB::lookup_history_100_desc(AccountId accountId, int64_t beforeId)
{
    return stmtHistoryById.all(
        [&](const sqlite::Row& row) {
            return std::tuple<HistoryId, history::Entry>(
                { row[0], { row[1], history::HistoryVariant(ReadExhaustive(std::vector<uint8_t>(row[2]))) } });
        },
        accountId, beforeId);
}

wrt::optional<Address> ChainDB::lookup_address(AccountId id) const
{
    return stmtAccountsLookup.one(id).process([](auto& o) {
        return Address { o[0] };
    });
}

auto ChainDB::get_token_balance(BalanceId id) const -> wrt::optional<BalanceData>
{
    return stmtSelectBalanceId.one(id).process([&](const sqlite::Row& o) {
        return BalanceData {
            { .accountId = o[0],
                .tokenId = o[1],
                .total = o[2],
                .locked = o[3] },
            id
        };
    });
}

std::vector<AssetDetail> ChainDB::lookup_assets_by_prefix(std::string_view prefix) const
{
    std::string prefixLike;
    prefixLike.resize(prefix.size() + 1);
    std::ranges::copy(prefix, prefixLike.begin());
    prefixLike[prefix.size()] = '%';
    return stmtAssetLookupByPrefix.all([](const auto& o) -> AssetDetail {
        return {
            AssetBasic {
                .id = o[0],
                .hash = o[1],
                .name = o[2],
                .precision = o[3],
            },
            {
                .height = o[4],
                .ownerAccountId = o[5],
                .totalSupply = o[6],
                .group_id = o[7],
                .parent_id = o[8],
            }
        };
    },
        prefixLike);
}

wrt::optional<AssetDetail> ChainDB::lookup_asset(AssetId id) const
{
    // , stmtAssetLookup(db, "SELECT (id, hash, name, precision, height, owner_account_id, total_supply, group_id, parent_id) FROM Assets WHERE `id`=?")
    return stmtAssetLookup.one(id).process([](auto& o) -> AssetDetail {
        return {
            AssetBasic {
                .id = o[0],
                .hash = o[1],
                .name = o[2],
                .precision = o[3],
            },
            {
                .height = o[4],
                .ownerAccountId = o[5],
                .totalSupply = o[6],
                .group_id = o[7],
                .parent_id = o[8],
            }
        };
    });
}

wrt::optional<AssetDetail> ChainDB::lookup_asset(const AssetHash& hash) const
{
    return stmtAssetLookupByHash.one(hash).process([](auto& o) -> AssetDetail {
        return {
            AssetBasic {
                .id = o[0],
                .hash = o[1],
                .name = o[2],
                .precision = o[3],
            },
            {
                .height = o[4],
                .ownerAccountId = o[5],
                .totalSupply = o[6],
                .group_id = o[7],
                .parent_id = o[8],
            }
        };
    });
}

wrt::optional<BlockId> ChainDB::lookup_block_id(const HashView hash) const
{
    return stmtBlockIdSelect.one(hash);
}

wrt::optional<NonzeroHeight> ChainDB::lookup_block_height(const HashView hash) const
{
    return stmtBlockHeightSelect.one(hash).process([](auto& r) {
        return NonzeroHeight { r[0] };
    });
}

void ChainDB::delete_bad_block(HashView blockhash)
{
    auto o = stmtBlockIdSelect.one(blockhash);
    if (!o.has_value()) {
        spdlog::error("Database error: Cannot delete bad block with hash {}",
            serialize_hex(blockhash));
        return;
    }
    BlockId id { o[0] };
    stmtBlockDelete.run(id);
    stmtScheduleDelete.run(id);
}

auto ChainDB::get_token_balance_recursive(AccountId aid, TokenId tid, api::AssetLookupTrace* trace) const -> RecursiveTokenLookup
{
    bool forked = false;
    while (true) {
        // direct lookup
        if (auto b { get_balance(aid, tid) })
            return { b->second, { b->first, forked } };
        forked = true;

        auto nw { tid.non_wart() };
        if (!nw || nw->is_liquidity())
            goto notfound; // WART and pool share token types cannot have any parent

        // get token fork height
        auto a { lookup_asset(nw->asset_id()) };
        if (!a)
            throw std::runtime_error("Database error: Cannot find asset info for id " + std::to_string(tid.value()) + ".");
        auto h { a->height };
        auto& pid { a->parent_id };
        if (!pid) // has no parent, i.e. was not forked, no entry found
            goto notfound;
        if (trace)
            trace->fails.push_back(*a);
        if (auto o { get_balance_snapshot_after(*pid, h) }) {
            auto& [height, funds] { *o };
            if (trace)
                trace->snapshotHeight = height;
            return { Balance_uint64::from_total_locked(funds, 0) };
        };
        tid = *pid;
    }
notfound:
    return { Balance_uint64::zero() };
}

Funds_uint64 ChainDB::get_free_balance(AccountToken at) const
{
    auto b { get_token_balance_recursive(at.account_id(), at.token_id(), nullptr) };
    return b.balance.free_assert();
}

chainserver::TransactionIds ChainDB::fetch_tx_ids(Height height) const
{
    const auto r = chainserver::TransactionIds::block_range(height);
    auto minPinHeight { height.add1().pin_begin() };
    chainserver::TransactionIds out;
    spdlog::debug("Loading nonces from blocks {} to {} into cache...", r.hbegin.value(), r.hend.value());
    auto ids { consensus_block_ids(r) };
    for (size_t i = 0; i < r.length(); ++i) {
        if ((i & 15) == 0 && shutdownSignal) {
            throw std::runtime_error("Shutdown initiated");
        }
        Height height = r.hbegin + i;
        auto id = ids[i];
        auto b { get_block(id) };
        if (!b)
            throw std::runtime_error("Database corrupted (consensus block id " + std::to_string(id.value()) + "+ not available)");
        assert(height == b->height);
        auto txids { b->tx_ids(minPinHeight) };

        for (auto& txid : txids.fromCancelations)
            out.insert(txid); // Cancelations can have collisions with txids from previous blocks, don't check for duplicates.

        // check txids.fromTransactions for duplicates in previous blocks transaction
        // ids AND same block cancelation ids, therefore this for loop comes second
        for (auto& tid : txids.fromTransactions) {
            if (out.emplace(tid).second == false) {
                throw std::runtime_error(
                    "Database corrupted (duplicate transaction id in chain)");
            };
        }
    }
    return out;
}
}

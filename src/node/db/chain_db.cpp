#include "chain_db.hpp"
#include "api/types/all.hpp"
#include "block/body/parse.hpp"
#include "block/chain/header_chain.hpp"
#include "block/header/header_impl.hpp"
#include "block/header/view_inline.hpp"
#include "general/hex.hpp"
#include "general/now.hpp"
#include "sqlite3.h"
#include <array>
#include <spdlog/spdlog.h>

ChainDB::Cache ChainDB::Cache::init(SQLite::Database& db)
{
    auto maxStateId = AccountId(int64_t(db.execAndGet("SELECT coalesce(max(ROWID),0) FROM `State`")
            .getInt64()));

    int64_t hid = db.execAndGet("SELECT coalesce(max(id)+1,1) FROM History")
                      .getInt64();
    if (hid < 0)
        throw std::runtime_error("Database corrupted, negative history id.");
    return {
        .maxStateId { maxStateId },
        .nextHistoryId = HistoryId { uint64_t(hid) },
        .deletionKey { 2 }
    };
}

ChainDBTransaction ChainDB::transaction()
{
    return ChainDBTransaction(*this);
}
ChainDB::ChainDB(const std::string& path)
    : db(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
    , fl(path)
    , createTables(db)
    , cache(Cache::init(db))
    , stmtBlockInsert(db, "INSERT INTO \"Blocks\" ( `height`, `header`, `body` "
                          ", `hash`) VALUES (?,?,?,?)")
    , stmtUndoSet(db, "UPDATE \"Blocks\" SET `undo`=? WHERE `ROWID`=?")
    , stmtBlockGetUndo(
          db, "SELECT `header`,`body`, `undo` FROM \"Blocks\" WHERE `ROWID`=?")
    , stmtBlockById(
          db, "SELECT `height`, `header`, `body` FROM \"Blocks\" WHERE `ROWID`=?;")
    , stmtBlockByHash(
          db, "SELECT ROWID, `height`, `header`, `body` FROM \"Blocks\" WHERE `hash`=?;")
    , stmtConsensusHeaders(db, "SELECT c.height, c.history_cursor, c.account_cursor, b.header "
                               "FROM `Blocks` b JOIN `Consensus` c ON "
                               "b.ROWID=c.block_id ORDER BY c.height ASC;")
    , stmtConsensusInsert(db, "INSERT INTO \"Consensus\" ( `height`, "
                              "`block_id`, `history_cursor`, `account_cursor`) VALUES (?,?,?,?)")
    , stmtConsensusSetProperty(
          db, "INSERT OR REPLACE INTO \"Consensus\" (`height`,`block_id`, `history_cursor`, `account_cursor`) VALUES (?,?,0,0)")
    , stmtConsensusSelect(
          db, "SELECT `block_id` FROM \"Consensus\" WHERE `height`=?")
    , stmtConsensusSelectRange(
          db, "SELECT `block_id` FROM \"Consensus\" WHERE `height`>=? AND "
              "`height` <? ORDER BY height ASC")
    , stmtConsensusSelectHistory(
          db, "SELECT `history_cursor` FROM \"Consensus\" WHERE `height`=?")
    , stmtConsensusHead(
          db, "SELECT `height`, `block_id` FROM \"Consensus\" ORDER BY "
              "`height` DESC LIMIT 1;")
    , stmtConsensusDeleteFrom(db, "DELETE FROM `Consensus` WHERE `height`>=?")

    , stmtScheduleExists(db, "SELECT EXISTS(SELECT 1 FROM `Deleteschedule` WHERE `block_id`=?)")
    , stmtScheduleInsert(db, "INSERT INTO `Deleteschedule` (`block_id`,`deletion_key`) VALUES (?,?)")
    , stmtScheduleBlock(db, "UPDATE `Deleteschedule` SET `deletion_key`=? WHERE `block_id` = ?")
    , stmtScheduleProtected(db, "UPDATE `Deleteschedule` SET `deletion_key`=? WHERE `deletion_key` = 0")
    , stmtScheduleDelete2(db, "DELETE FROM `Deleteschedule` WHERE `block_id` = ?")
    , stmtScheduleConsensus(db, "REPLACE INTO `Deleteschedule` (block_id,deletion_key) SELECT block_id, ? FROM Consensus WHERE height >= ?")

    , stmtDeleteGCBlocks(
          db, "DELETE FROM `Blocks` WHERE ROWID IN (SELECT `block_id`  FROM "
              "`Deleteschedule` WHERE `deletion_key`<=? AND `deletion_key` > 0 )")
    , stmtDeleteGCRefs(db, "DELETE  FROM `Deleteschedule` WHERE `deletion_key`<=? AND `deletion_key` > 0")

    , stmtStateInsert(db, "INSERT INTO \"State\" ( `ROWID`, `address`, "
                          "`balance`) VALUES (?,?,?)")
    , stmtStateDeleteFrom(db, "DELETE FROM `State` WHERE `ROWID`>=?")
    , stmtStateSetBalance(db, "UPDATE `State` SET `balance`=? WHERE `ROWID`=?")

    , stmtBadblockInsert(
          db, "INSERT INTO `Badblocks` (`height`, `header`) VALUES (?,?)")
    , stmtBadblockGet(db, "SELECT `height`, `header` FROM `Badblocks`")
    , stmtAccountLookup(
          db, "SELECT `Address`, `Balance` FROM `State` WHERE ROWID=?")
    , stmtRichlistLookup(
          db, "SELECT Address, Balance FROM `State` ORDER BY `Balance` DESC LIMIT ?")
    , stmtHistoryInsert(db, "INSERT INTO `History` (`id`,`hash`, `data`"
                            ") VALUES (?,?,?)")
    , stmtHistoryDeleteFrom(db, "DELETE FROM `History` WHERE `id`>=?")
    , stmtHistoryLookupByHash(db,
          "SELECT `id`, `data` FROM `History` WHERE `hash`=? LIMIT 1")
    , stmtHistoryLookupById(db,
          "SELECT `hash`, `data` FROM `History` WHERE `id`=?")
    , stmtHistoryLookupRange(db,
          "SELECT `hash`, `data` FROM `History` WHERE `id`>=? AND`id`<?")
    , stmtAccountHistoryInsert(db, "INSERT INTO `AccountHistory` "
                                   "(`account_id`,`history_id`) VALUES (?,?)")
    , stmtAccountHistoryDeleteFrom(
          db, "DELETE FROM `AccountHistory` WHERE `history_id`>=?")
    , stmtBlockIdSelect(
          db, "SELECT `ROWID` FROM `Blocks` WHERE `hash`=?")
    , stmtBlockHeightSelect(
          db, "SELECT `height` FROM `Blocks` WHERE `hash`=?")
    , stmtBlockDelete(db, "DELETE FROM `Blocks` WHERE ROWID = ?")

    // BELOW STATEMENTS REQUIRED FOR INDEXING NODES
    //
    , stmtAddressLookup(
          db, "SELECT `ROWID`,`balance` FROM `State` WHERE `address`=?")
    , stmtHistoryById(db, "SELECT h.id, `hash`,`data` FROM `History` `h` JOIN "
                          "`AccountHistory` `ah` ON h.id=`ah`.history_id WHERE "
                          "ah.`account_id`=? AND h.id<? ORDER BY h.id DESC LIMIT 100")
{

    //
    // Do DELETESCHEDULE cleanup
    db.exec("UPDATE `Deleteschedule` SET `deletion_key`=1");
}

void ChainDB::insertStateEntry(const AddressView address, Funds balance,
    AccountId verifyNextStateId)
{
    if (cache.maxStateId + 1 != verifyNextStateId)
        throw std::runtime_error("Internal error, state id inconsistent.");
    stmtStateInsert.run(cache.maxStateId + 1, address, balance);
    cache.maxStateId++;
}

void ChainDB::delete_state_from(AccountId fromAccountId)
{
    assert(fromAccountId.value() > 0);
    if (cache.maxStateId + 1 < fromAccountId) {
        spdlog::error("BUG: Deleting nothing, fromAccountId = {} > {} = cache.maxStateId", fromAccountId.value(), cache.maxStateId.value());
    } else {
        cache.maxStateId = fromAccountId - 1;
        stmtStateDeleteFrom.run(fromAccountId);
    }
}

Worksum ChainDB::get_consensus_work() const
{
    auto o { stmtConsensusSelect.one(WORKSUMID) };
    if (!o.has_value()) {
        throw std::runtime_error("Database corrupted. No worksum entry");
    }
    return o.get<std::array<uint8_t, 32>>(0);
}
void ChainDB::set_consensus_work(const Worksum& ws)
{
    stmtConsensusSetProperty.run(WORKSUMID, ws);
}

std::optional<SignedSnapshot> ChainDB::get_signed_snapshot() const
{
    auto o { stmtConsensusSelect.one(SIGNEDPINID) };
    if (!o.has_value()) {
        return {};
    }
    std::vector<uint8_t> v { o.get_vector(0) };
    Reader r(v);
    try {
        return SignedSnapshot(r);
    } catch (Error e) {
        throw std::runtime_error(spdlog::fmt_lib::format("Database corrupted. Signed snapshot invalid: {}", e.strerror()));
    }
}

void ChainDB::set_signed_snapshot(const SignedSnapshot& ss)
{
    std::vector<uint8_t> v(SignedSnapshot::binary_size);
    Writer w(v);
    w << ss;
    stmtConsensusSetProperty.run(SIGNEDPINID, v);
}

std::vector<BlockId> ChainDB::consensus_block_ids(Height begin,
    Height end) const
{
    assert(end >= begin);
    std::vector<BlockId> out;
    stmtConsensusSelectRange.for_each([&](Statement2::Row& r) {
        out.push_back(BlockId(r.get<int64_t>(0)));
    },
        begin, end);
    return out;
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
    assert(stmtScheduleBlock.run(0, id.value()) == 1);
}

DeletionKey ChainDB::delete_consensus_from(NonzeroHeight height)
{
    auto dk { cache.deletionKey++ };
    stmtScheduleConsensus.run(dk.value(), height);
    stmtConsensusDeleteFrom.run(height);
    return dk;
}

std::optional<Block> ChainDB::get_block(BlockId id) const
{
    auto o { stmtBlockById.one(id) };
    if (!o.has_value())
        return {};
    Height h { o.get<Height>(0) };
    if (h == 0) {
        throw std::runtime_error("Database corrupted, block has height 0");
    }
    return Block {
        .height = h.nonzero_assert(),
        .header = o.get_array<80>(1),
        .body = o.get_vector(2)
    };
}

std::optional<std::pair<BlockId, Block>> ChainDB::get_block(HashView hash) const
{
    auto o = stmtBlockByHash.one(hash);
    if (!o.has_value())
        return {};
    Height h { o.get<Height>(0) };
    if (h == 0) {
        throw std::runtime_error("Database corrupted, block has height 0");
    }
    return std::pair<BlockId, Block> {
        o.get<BlockId>(0),
        Block {
            .height = h.nonzero_assert(),
            .header = o.get_array<80>(2),
            .body = o.get_vector(3) }
    };
}

std::pair<BlockId, bool> ChainDB::insert_protect(const Block& b)
{
    auto hash { b.header.hash() };

    auto blockId { lookup_block_id(hash) };
    if (blockId.has_value()) {
        assert(schedule_exists(*blockId) || consensus_exists(b.height, *blockId));
        return { blockId.value(), false };
    } else {
        stmtBlockInsert.run(b.height, b.header, b.body.data(), hash);
        auto lastId { db.getLastInsertRowid() };
        stmtScheduleInsert.run(lastId, 0);
        return { BlockId(lastId), true };
    }
}

std::optional<std::tuple<Header, RawBody, RawUndo>>
ChainDB::get_block_undo(BlockId id) const
{
    auto a = stmtBlockGetUndo.one(id);
    if (!a.has_value())
        return {};
    return std::tuple<Header, RawBody, RawUndo> {
        a.get_array<80>(0),
        { a.get_vector(1) },
        { a.get_vector(2) }
    };
}

void ChainDB::set_block_undo(BlockId id, const std::vector<uint8_t>& undo)
{
    stmtUndoSet.run(undo, id);
}

void ChainDB::insert_consensus(NonzeroHeight height, BlockId blockId, HistoryId historyCursor, AccountId accountCursor)
{
    stmtConsensusInsert.run(height, blockId, historyCursor, accountCursor);
    stmtScheduleDelete2.run(blockId);
}

std::tuple<std::vector<Batch>, HistoryHeights, AccountHeights> ChainDB::getConsensusHeaders() const
{
    uint32_t h = 1;
    std::vector<Batch> batches;
    ;
    HistoryHeights historyHeights;
    AccountHeights accountHeights;
    Batch b;
    stmtConsensusHeaders.for_each([&](Statement2::Row& r) {
        if (h != r.get<Height>(0)) { // corrupted
            throw std::runtime_error("Database corrupted, block height not consecutive");
        }
        historyHeights.append(r.get<HistoryId>(1));
        accountHeights.append(r.get<AccountId>(2));
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
    std::vector<std::pair<Height, Header>> res;
    stmtBadblockGet.for_each([&](Statement2::Row& r) {
        res.push_back({ r.get<Height>(0),
            r.get_array<80>(1) });
    });
    return res;
}

HistoryId ChainDB::insertHistory(const HashView hash,
    const std::vector<uint8_t>& data)
{
    stmtHistoryInsert.run((int64_t)cache.nextHistoryId.value(), hash, data);
    return cache.nextHistoryId++;
}

void ChainDB::delete_history_from(NonzeroHeight h)
{
    const int64_t nextHistoryId = stmtConsensusSelectHistory.one(h).get<int64_t>(0);
    assert(nextHistoryId >= 0);
    stmtHistoryDeleteFrom.run(nextHistoryId);
    stmtAccountHistoryDeleteFrom.run(nextHistoryId);
    cache.nextHistoryId = HistoryId { nextHistoryId };
}

std::optional<std::pair<std::vector<uint8_t>, HistoryId>> ChainDB::lookup_history(const HashView hash)
{
    auto o = stmtHistoryLookupByHash.one(hash);
    if (!o.has_value())
        return {};
    auto index { HistoryId { o.get<int64_t>(0) } };
    assert(index > HistoryId { 0 });
    return std::pair {
        o.get_vector(1),
        index
    };
}
std::optional<std::pair<std::vector<uint8_t>, Hash>> ChainDB::lookup_history(HistoryId hid)
{
    auto o = stmtHistoryLookupById.one(hid.value());
    if (!o.has_value())
        return {};
    Hash hash { o.get_array<32>(0) };
    return std::pair {
        o.get_vector(1),
        hash
    };
}

std::vector<std::pair<Hash, std::vector<uint8_t>>> ChainDB::lookupHistoryRange(HistoryId lower, HistoryId upper)
{
    std::vector<std::pair<Hash, std::vector<uint8_t>>> out;
    int64_t l = lower.value();
    int64_t u = (upper == HistoryId { 0 } ? std::numeric_limits<int64_t>::max() : upper.value());
    stmtHistoryLookupRange.for_each([&](Statement2::Row& r) {
        out.push_back(
            { r.get_array<32>(0),
                r.get_vector(1) });
    },
        l, u);
    return out;
}

void ChainDB::insertAccountHistory(AccountId accountId, HistoryId historyId)
{
    stmtAccountHistoryInsert.run(accountId, historyId);
}

std::optional<AccountFunds> ChainDB::lookup_address(const AddressView address) const
{
    auto p = stmtAddressLookup.one(address);
    if (!p.has_value())
        return {};
    return AccountFunds {
        p.get<AccountId>(0),
        p.get<Funds>(1)
    };
}

std::vector<std::tuple<HistoryId, Hash, std::vector<uint8_t>>> ChainDB::lookup_history_100_desc(
    AccountId accountId, int64_t beforeId)
{
    std::vector<std::tuple<HistoryId, Hash, std::vector<uint8_t>>> out;
    stmtHistoryById.for_each(
        [&](Statement2::Row& row) {
            out.push_back({ HistoryId { row.get<uint64_t>(0) },
                row.get_array<32>(1),
                row.get_vector(2) });
        },
        accountId, beforeId);
    return out;
}

AddressFunds ChainDB::fetch_account(AccountId id) const
{
    auto p = lookup_account(id);
    if (!p) {
        throw std::runtime_error("Database corrupted (fetch_account(" + std::to_string(id.value()) + ")");
    }
    return *p;
}

std::optional<AddressFunds> ChainDB::lookup_account(AccountId id) const
{
    auto o { stmtAccountLookup.one(id) };
    if (!o.has_value())
        return {};
    return AddressFunds {
        .address = o.get_array<20>(0),
        .funds = o.get<Funds>(1)
    };
}

API::Richlist ChainDB::lookup_richlist(uint32_t N) const
{
    API::Richlist out;
    stmtRichlistLookup.for_each([&](Statement2::Row& r) {
        out.entries.push_back(
            { Address { r.get_array<20>(0) },
                r.get<Funds>(1) });
    },
        N);
    return out;
}

std::optional<BlockId> ChainDB::lookup_block_id(const HashView hash) const
{
    return stmtBlockIdSelect.one(hash);
}

std::optional<NonzeroHeight> ChainDB::lookup_block_height(const HashView hash) const
{
    auto o { stmtBlockHeightSelect.one(hash) };
    if (!o.has_value())
        return {};
    auto h { o.get<Height>(0) };
    if (h == 0) {
        throw std::runtime_error("Database corrupted, block " + serialize_hex(hash) + " has invalid height 0.");
    }
    return h.nonzero_assert();
}

void ChainDB::delete_bad_block(HashView blockhash)
{
    auto o = stmtBlockIdSelect.one(blockhash);
    if (!o.has_value()) {
        spdlog::error("Database error: Cannot delete bad block with hash {}",
            serialize_hex(blockhash));
        return;
    }
    BlockId id { o.get<BlockId>(0) };
    stmtBlockDelete.run(id);
    stmtScheduleDelete2.run(id);
}

namespace {
std::vector<TransactionId> read_tx_ids(const BodyContainer& body,
    NonzeroHeight height)
{
    BodyView bv(body.view(height));
    if (!bv.valid())
        throw std::runtime_error(
            "Database corrupted (invalid block body at height " + std::to_string(height) + ".");
    PinFloor pinFloor { PrevHeight(height) };

    std::vector<TransactionId> out;
    for (auto t : bv.transfers()) {
        auto txid { t.txid(t.pinHeight(pinFloor)) };
        out.push_back(txid);
    }
    return out;
}
} // namespace

chainserver::TransactionIds ChainDB::fetch_tx_ids(Height height) const
{
    const auto [begin, end] = chainserver::TransactionIds::block_range(height);
    chainserver::TransactionIds out;
    spdlog::debug("Loading nonces from blocks {} to {} into cache...", begin.value(), end.value());
    auto ids { consensus_block_ids(begin, end) };
    if (ids.size() != end - begin)
        throw std::runtime_error("Cannot load block ids.");
    for (size_t i = 0; i < end - begin; ++i) {
        Height height = begin + i;
        auto id = ids[i];
        auto b = get_block(id);
        if (!b) {
            throw std::runtime_error("Database corrupted (consensus block id " + std::to_string(id.value()) + "+ not available)");
        }
        assert(height == b->height);
        assert(b->body.size() > 0);
        for (auto& tid : read_tx_ids(b->body, b->height)) {
            if (out.emplace(tid).second == false) {
                throw std::runtime_error(
                    "Database corrupted (duplicate transaction id in chain)");
            };
        }
    }
    return out;
}

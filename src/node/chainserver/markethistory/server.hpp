#pragma once
#include "../server_fwd.hpp"
#include "api/enable_api.hpp"
#include "api/types/input.hpp"
#include "api/types/opt_param.hpp"
#include "block/chain/fork_range.hpp"
#include "block/chain/header_chain.hpp"
#include "trades_db.hpp"
#include "wrt/variant.hpp"
#include <condition_variable>
#include <list>
#include <shared_mutex>
#include <thread>
#include <vector>

using CandlesCallback = std::function<void(Result<api::CandlesVector>)>;
using TradesCallback = std::function<void(Result<api::TradesVector>)>;
#define LIST_API_TYPES(XX)                                        \
    XX(GetCandles, api::CandlesVector, api::AssetIdOrHash, asset, \
        std::string, interval, OptParam<Timestamp>, from,         \
        OptParam<Timestamp>, to, OptParam<size_t>, N)             \
    XX(GetTrades, api::TradesVector, api::AssetIdOrHash, asset,   \
        OptParam<NonzeroHeight>, from, OptParam<NonzeroHeight>,   \
        to, OptParam<size_t>, N)

DEFINE_TYPE_COLLECTION(APIReadTypes, LIST_API_TYPES);
#undef LIST_API_TYPES
namespace market_history {

class ReaderThreadpool;

struct HasAsset {
    api::AssetIdOrHash asset;
};
struct GetTradesBase : HasAsset {
    TradesCallback callback;
    GetTradesBase(api::AssetIdOrHash asset, TradesCallback callback)
        : HasAsset(std::move(asset))
        , callback(std::move(callback))
    {
    }
};

struct GetTradesFrom : GetTradesBase {
    NonzeroHeight from;
    size_t N;
};
struct GetTradesTo : GetTradesBase {
    NonzeroHeight to;
    size_t N;
};
struct GetTradesRange : GetTradesBase {
    NonzeroHeight from;
    NonzeroHeight to;
};
struct GetTradesLatest : GetTradesBase {
    size_t N;
};

struct GetCandlesBase : HasAsset {
    Interval interval;
    CandlesCallback callback;
    GetCandlesBase(api::AssetIdOrHash asset, Interval interval, CandlesCallback callback)
        : HasAsset(std::move(asset))
        , interval(std::move(interval))
        , callback(std::move(callback))
    {
    }
};
struct GetCandlesFrom : public GetCandlesBase {
    Timestamp from;
    size_t N;
};
struct GetCandlesTo : public GetCandlesBase {
    Timestamp to;
    size_t N;
};
struct GetCandlesRange : public GetCandlesBase {
    Timestamp from;
    Timestamp to;
};
struct GetCandlesLatest : public GetCandlesBase {
    size_t N;
};

using ReaderEventInternal = wrt::variant<
    GetTradesRange,
    GetTradesFrom,
    GetTradesTo,
    GetTradesLatest,
    GetCandlesRange,
    GetCandlesFrom,
    GetCandlesTo,
    GetCandlesLatest>;
class Reader {
public:
    Reader(MarketReaderDB&& db, ReaderThreadpool& parent)
        : db(std::move(db))
        , parent(parent)
    {
        worker = std::jthread([&] { work(); });
    }
    ~Reader()
    {
        worker.join();
    }

private:
    void work();
    void dispatch(ReaderEventInternal&& event);
    Result<Asset> normalize(const api::AssetIdOrHash& asset);
    void handle_event(const Asset&, GetCandlesRange&&);
    void handle_event(const Asset&, GetCandlesFrom&&);
    void handle_event(const Asset&, GetCandlesTo&&);
    void handle_event(const Asset&, GetCandlesLatest&&);
    void handle_event(const Asset&, GetTradesRange&&);
    void handle_event(const Asset&, GetTradesFrom&&);
    void handle_event(const Asset&, GetTradesTo&&);
    void handle_event(const Asset&, GetTradesLatest&&);

private:
    MarketReaderDB db;
    ReaderThreadpool& parent;
    std::jthread worker;
};

using ReaderEvent = wrt::variant<GetCandles, GetTrades>;

class ReaderThreadpool : public enable_api_methods<ReaderThreadpool, APIReadTypes> {
    friend class Reader;

public:
    ReaderThreadpool(MarketDb& db, size_t N);
    ReaderThreadpool(const ReaderThreadpool&) = delete; // no copy, address needs to stay constant as we have references in the Readers.
    ~ReaderThreadpool()
    {
        shutdown();
    }
    void defer(ReaderEventInternal&& e)
    {
        std::lock_guard l(m);
        readerEvents.push_back(std::move(e));
        cv.notify_one();
    }
    void shutdown()
    {
        std::lock_guard l(m);
        _shutdown = true;
        cv.notify_all();
    }
    MarketReaderDB clone_reader() const;

private:
    bool _shutdown { false };
    std::vector<std::unique_ptr<Reader>> readers;
    std::condition_variable cv;
    std::mutex m;
    std::list<ReaderEventInternal> readerEvents;
};

class MarketHistoryServer {
    friend class ReaderThreadpool;

    struct OnChainAppend {
        BlockInfo block;
    };
    struct BlockReply {
        BlockInfo blockInfo;
        Descriptor descriptor;
    };
    struct BoundsReply {
        RollbackBounds rollbackBounds; // needed for initial db rollback
        Descriptor descriptor;
    };
    struct OnChainReplace {
        Headerchain headerchain;
        Descriptor descriptor;
        RollbackBounds rollbackBounds; // needed for db rollback
    };

public:
    using Event = wrt::variant<
        OnChainAppend,
        BlockReply,
        BoundsReply,
        OnChainReplace>;

    void api_call(GetCandles::Object&& e);
    void api_call(GetTrades::Object&& e);
    void defer(Event e)
    {
        std::lock_guard l(m);
        events.push_back(std::move(e));
        cv.notify_all();
    }

    template <typename T>
    static constexpr bool supports = ReaderThreadpool::supports<T>;

    struct InitData {
        MarketDb& db;
        ChainServer& chainServer;
        Headerchain consensusCopy;
        Descriptor consensusDescriptor;
        size_t readerCount { 1 };
    };
    MarketHistoryServer(InitData data);
    ~MarketHistoryServer()
    {
        shutdown();
        readers.shutdown();
        worker.join();
    }

private:
    void work();
    void shutdown()
    {
        std::lock_guard l(m);
        _shutdown = true;
        cv.notify_all();
    }
    ReaderEventInternal wrap_event_throw(GetCandles::Object&& e);
    ReaderEventInternal wrap_event_throw(GetTrades::Object&& e);
    void defer(ReaderEventInternal e)
    {
        readers.defer(std::move(e));
    }
    void handle_event(OnChainAppend&&);
    void handle_event(BlockReply&&);
    void handle_event(BoundsReply&&);
    void handle_event(OnChainReplace&&);
    void transaction_rollback(const RollbackBounds& nextBounds);
    void try_request_block();
    void try_initial_rollback();

private:
    MarketDb& db;
    std::optional<NonzeroHeight> scheduledRollbackHeight; // has value if we are still initializing
    ChainServer& chainServer;
    Headerchain consensusCopy;
    Descriptor consensusDescriptor;

    ForkRange fr; // fork range with respect to consensus chain, this server shall always catch up to consensus chain.
    bool pendingRequest { false }; // whether a request to the chainserver is pending
    std::thread worker;
    std::condition_variable cv;
    std::mutex m;
    bool _shutdown { false };
    std::shared_mutex sqliteLock;
    std::vector<Event> events;

    ReaderThreadpool readers;
};

}
using MarketHistoryServer = market_history::MarketHistoryServer;

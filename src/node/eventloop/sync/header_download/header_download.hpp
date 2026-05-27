#pragma once
#include "../request_sender_declaration.hpp"
#include "block/chain/consensus_headers.hpp"
#include "block/chain/offender.hpp"
#include "eventloop/types/conndata.hpp"
#include "eventloop/types/peer_requests.hpp"
#include <deque>
#include <set>

struct Inspector;
namespace HeaderDownload {

struct LeaderInfo {
    Conref cr;
    std::shared_ptr<Descripted> descripted;
};

struct VerifierNode;
using VerifierMap = std::map<Header, VerifierNode, HeaderView::HeaderComparator>;
struct RequestNode;
using RequestMap = std::map<Header, RequestNode>;
using Ver_iter = VerifierMap::iterator;
using Req_iter = RequestMap::iterator;
using Variant = std::variant<std::monostate, Req_iter, Ver_iter>;

struct ReqData;
struct RequestNode {
    RequestNode()
    {
    }

    Conref cr;
    uint64_t originId;
    Batch batch;

    // methods
    bool filled() const { return batch.size() > 0; }
    bool pending() const { return cr.valid(); }
};

struct LeaderNode;
using Lead_list = std::list<LeaderNode>;
using Lead_iter = Lead_list::iterator;
using Lead_set = std::set<Lead_iter>;

inline bool operator<(const Lead_iter& l1, const Lead_iter& l2)
{
    static_assert(sizeof(uint64_t) == sizeof(l1));
    return *reinterpret_cast<const uint64_t*>(&l1) < *reinterpret_cast<const uint64_t*>(&l2);
}
struct NonzeroSnapshot {
    NonzeroSnapshot(std::shared_ptr<Descripted>);
    std::shared_ptr<Descripted> descripted;
    NonzeroHeight length;
    Worksum worksum;
};
struct VerifierNode {
    VerifierNode(SharedBatch&& b);
    VerifierNode(SharedBatch&& b, HeaderVerifier&& hv);
    size_t refcount = 0;
    HeaderVerifier verifier;
    // Lead_vec leaders;
    SharedBatch sb;
};
inline bool operator<(const Ver_iter& l1, const Ver_iter& l2)
{
    static_assert(sizeof(uint64_t) == sizeof(l1));
    return *reinterpret_cast<const uint64_t*>(&l1) < *reinterpret_cast<const uint64_t*>(&l2);
}
struct QueueBatchNode {
    Conref cr;
    uint64_t originId;
    Batch batch;
    Lead_set leaderRefs;
    std::vector<Conref> probeRefs;
    bool has_pending_request() { return cr.valid(); }
};
using Queued_iter = std::map<Header, QueueBatchNode>::iterator;
inline bool operator<(const Queued_iter& l1, const Queued_iter& l2)
{
    static_assert(sizeof(uint64_t) == sizeof(l1));
    return *reinterpret_cast<const uint64_t*>(&l1) < *reinterpret_cast<const uint64_t*>(&l2);
}

struct QueueEntry {
    std::optional<Header> prevHeader;
    Queued_iter iter;
};

struct LeaderNode {
public:
    LeaderNode(Conref cr, NonzeroSnapshot&& snapshot, ProbeData&& pd, VerifierMap::iterator verifier)
        : cr(cr)
        , snapshot(std::move(snapshot))
        , verifier { verifier }
        , probeData(std::move(pd))
    {
    }
    LeaderNode(Conref cr, NonzeroSnapshot&& snapshot, ProbeData&& pd) // with no verifier reference (starts from genesis)
        : cr(cr)
        , snapshot(std::move(snapshot))
        , probeData(std::move(pd))
    {
    }
    const Conref cr;

    // Nodes will follow downloading past descripted headers from leaders to prevent header/work spoofing attacks
    // where attacking nodes change their advertised longest chain quickly. Nodes will still download the advertised
    // chain they were interested in first. To this end they save the chain descriptor, length and
    // total work in the `snapshot` variable.
    const NonzeroSnapshot snapshot;

    struct {
        Batch batch;
        Worksum claimedWork;
    } finalBatch;
    Batchslot final_slot()
    {
        return Batchslot(snapshot.length);
    }

    std::deque<QueueEntry> queuedIters; // OK
    std::optional<Ver_iter> verifier; // without value if from genesis
    ProbeData probeData;
    Batchslot next_slot();
    Worksum verified_total_work();
    Worksum final_batch_work();

private:
    struct Queued {
        LeaderNode& ln;
        struct iterator {
            size_t i = 0;
            Queued& q;
            iterator& operator++()
            {
                i += 1;
                return *this;
            }
            bool is_solo() const
            {
                if (q.ln.queuedIters.size() != 1)
                    return false;
                assert(i == 0);
                return true;
            }

            std::optional<ChainPin> pin_prev() const
            {
                auto& entry = q.ln.queuedIters[i];
                if (entry.prevHeader) {
                    return ChainPin { slot().offset(), *entry.prevHeader };
                }
                return {};
            }

            operator ReqData() const;

            auto& operator*() { return *this; }
            auto& node() { return q.ln.queuedIters[i].iter->second; }
            Batchslot slot() const { return q.ln.next_slot() + i; }
        };
        struct sentinel {
        };

        iterator begin()
        {
            return { 0, *this };
        }
        sentinel end() { return {}; }
        friend bool operator==(const iterator& iter, const sentinel&)
        {
            return iter.i >= iter.q.ln.queuedIters.size();
        }
    };

public:
    Queued queued()
    {
        return { *this };
    }
};

class Downloader {
    friend struct ::Inspector;

    struct Offender {
        ChainOffender co;
        Conref internal;
        Offender(ChainError ce, Conref cr)
            : co(ce, cr.id())
            , internal(cr) { };
    };

public:
private:
    auto& data(Conref cr) { return cr->usage.data_headerdownload; }
    auto& data(Conref cr) const { return cr->usage.data_headerdownload; }
    //
    // probe link functions
    void clear_connection_probe(Conref cr, bool eraseFromContainer);
    void set_connection_probe(Conref cr, ProbeData&& d, std::shared_ptr<Descripted>, Queued_iter iter);
    void clear_leader_probe(Lead_iter);
    //
    // leader functions
    void prune_leaders();
    bool is_leader(Conref cr) const
    {
        return data(cr).leaderIter != leaderList.end();
    }

    struct ConnectionFinder {
        ConnectionFinder(RequestSender& s, std::vector<Conref>& v)
            : s(s)
            , connections(v)
        {
        }
        RequestSender& s;
        const std::vector<Conref>& connections;
        size_t conIndex = 0;
    };

    void find(HeaderView hv);
    std::pair<Lead_set, Lead_set> split_leaders(const Lead_set& leaders, HeaderView h, size_t batchIndex);
    void process_req_iter(Req_iter, std::vector<Offender>&);

public:
    [[nodiscard]] bool is_active() const
    {
        return leaderList.size() > 0;
    }
    size_t size() const { return connections.size(); }
    Downloader(const StageAndConsensus& cache, Worksum minWork)
        : chains(cache)
        , minWork(minWork)
    {
    }
    [[nodiscard]] BanList set_min_worksum(const Worksum& ws);

    // peer message callbacks
    [[nodiscard]] BanList on_append(Conref cr);
    [[nodiscard]] BanList on_fork(Conref cr);
    [[nodiscard]] BanList on_rollback(Conref cr);

    BanList on_signed_snapshot_update();
    [[nodiscard]] BanList insert(Conref cr);
    [[nodiscard]] std::optional<BanList> erase(Conref cr);

    auto leaders_end() { return leaderList.end(); }

    [[nodiscard]] BanList do_header_requests(RequestSender);
    void do_probe_requests(RequestSender);

    void on_request_expire(Conref cr, const Batchrequest& msg);
    void on_proberep(Conref c, const Proberequest& req, const ProberepMsg&);
    void on_probe_request_expire(Conref cr);
    [[nodiscard]] BanList on_response(Conref cr, Batchrequest&&, Batch&&);
    struct header_download_t {
        /* data */
    };
    [[nodiscard]] std::optional<std::tuple<LeaderInfo, Headerchain, BanList>> pop_data();

private:
    bool do_exclusive_final_requests(RequestSender&);
    [[nodiscard]] BanList do_shared_grid_requests(RequestSender&);

    bool has_data() const;
    void select_leaders(BanList& banList);

    void process_final(Lead_iter, std::vector<Offender>& out);

    Conref try_send(ConnectionFinder& cf, BanList& banList, const ReqData&);
    bool try_final_request(LeaderNode&, RequestSender& s);

    [[nodiscard]] BanList filter_leadermismatch_offenders(std::vector<Offender>);

    // verifier related
    bool advance_verifier(const Ver_iter* vi, const Lead_set&, const Batch& b,
        std::vector<Offender>& out);
    Ver_iter acquire_verifier(SharedBatch&&);
    void release_verifier(Ver_iter);
    void verify_queued(Queued_iter qi, const Lead_set& leaders, std::vector<Offender>& out);

    // queued batch related
    std::map<Header, QueueBatchNode> queuedBatches;
    void acquire_queued_batch(std::optional<Header>, HeaderView, Lead_iter);
    void release_first_queued_batch(Lead_iter);
    void shift_queued_batch(Lead_iter);

    // leader related functions
    void erase_leader(Lead_iter);
    void queue_requests(Lead_iter);
    bool consider_insert_leader(Conref cr, BanList&); // returns true if has effect
    bool can_insert_leader(Conref cr);

    bool valid_shared_batch(const SharedBatch&);

private: // data
    VerifierMap verifierMap;
    std::optional<std::tuple<LeaderInfo, HeaderchainSkeleton, Worksum>> maximizer;
    size_t pendingDepth = 10;
    size_t maxLeaders = 10;

    Lead_list leaderList;
    std::vector<Conref> connections;
    std::vector<Conref> connectionsWithProbeJob;
    const StageAndConsensus& chains;
    Worksum minWork;
};
}

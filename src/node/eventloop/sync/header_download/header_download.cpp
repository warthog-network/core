#include "header_download.hpp"
#include "asyncio/connection.hpp"
#include "block/chain/consensus_headers.hpp"
#include "eventloop/eventloop.hpp"
#include "eventloop/types/peer_requests.hpp"
#include "global/globals.hpp"
#include "probe_balanced.hpp"
#include <set>
#include <stack>

namespace HeaderDownload {

struct ReqData {
    HeaderView finalHeader;
    QueueEntry queueEntry;
    Batchslot slot;
    std::optional<ChaincacheMatch> cacheMatch;
};

LeaderNode::Queued::iterator::operator ReqData() const
{
    auto& entry = q.ln.queuedIters[i];
    auto& finalHeader = entry.iter->first;
    return { finalHeader, entry, slot(), {} };
}

void Downloader::clear_connection_probe(Conref cr, bool eraseFromContainer = true)
{
    if (data(cr).probeData) {
        assert(std::erase(connectionsWithProbeJob, cr) == 1);
        if (eraseFromContainer) {
            auto& node = data(cr).probeData->qiter->second;
            assert(std::erase(node.probeRefs, cr) == 1);
        }
        data(cr).probeData.reset();
    }
}

void Downloader::set_connection_probe(Conref cr, ProbeData&& d, std::shared_ptr<Descripted> desc, Queued_iter iter)
{
    iter->second.probeRefs.push_back(cr);
    assert(!data(cr).probeData);
    data(cr).probeData = { std::move(d), std::move(desc), iter };
    connectionsWithProbeJob.push_back(cr);
}

Batchslot LeaderNode::next_slot()
{
    if (verifier.has_value())
        return (*verifier)->second.sb.slot().value() + 1;
    return Batchslot(0);
}

Worksum LeaderNode::verified_total_work()
{
    if (verifier)
        return (*verifier)->second.sb.total_work();
    return {};
}

Worksum LeaderNode::final_batch_work()
{
    return finalBatch.batch.worksum(final_slot().offset());
}

NonzeroSnapshot::NonzeroSnapshot(std::shared_ptr<Descripted> d)
    : descripted(std::move(d))
    , length(descripted->chain_length().nonzero_assert())
    , worksum(descripted->worksum())
{
    assert(!worksum.is_zero());
    spdlog::debug("Constructing snapshot with length {} and work {}", length.value(), worksum.getdouble());
}
VerifierNode::VerifierNode(SharedBatch&& b)
    : verifier(b.verifier())
    , sb(std::move(b))
{
}

VerifierNode::VerifierNode(SharedBatch&& b, HeaderVerifier&& hv)
    : verifier(std::move(hv))
    , sb(b)
{
}

Ver_iter Downloader::acquire_verifier(SharedBatch&& pin)
{
    HeaderView hv = pin.getBatch().last();
    auto vi = verifierMap.try_emplace(hv, std::move(pin)).first;
    vi->second.refcount += 1;
    return vi;
}
void Downloader::release_verifier(Ver_iter vi)
{
    auto& refcount = vi->second.refcount;
    assert(refcount > 0);
    if (--refcount == 0) {
        verifierMap.erase(vi);
    }
}

void Downloader::acquire_queued_batch(std::optional<Header> prev, HeaderView hv, Lead_iter li)
{
    auto p = queuedBatches.try_emplace(hv);
    auto iter = p.first;
    assert(iter->second.leaderRefs.insert(li).second);
    li->queuedIters.push_back({ prev, iter });
    assert(li->queuedIters.size() <= pendingDepth);
}

void Downloader::release_first_queued_batch(Lead_iter li)
{
    assert(li->queuedIters.size() > 0);
    auto iter = li->queuedIters.front().iter;
    li->queuedIters.erase(li->queuedIters.begin());

    // erase leader refs
    assert(iter->second.leaderRefs.erase(li) > 0);
    if (iter->second.leaderRefs.empty()) {
        // release probe refs
        for (auto cr : iter->second.probeRefs) {
            clear_connection_probe(cr, false);
        }
        auto cr(iter->second.cr);
        if (cr) {
            assert(data(cr).jobPtr == &iter->second);
            iter->second.cr.clear();
            data(cr).jobPtr = nullptr;
        }
        queuedBatches.erase(iter);
    }
}

bool Downloader::can_insert_leader(Conref cr)
{
    auto& id { data(cr).ignoreDescriptor };
    auto& d { cr.chain().descripted() };
    auto version_ok = [](Conref cr) {
        auto v { cr->c->peer_version() };
        return (v.minor() >= 8 || (v.minor() == 6 && v.patch() >= 21));
    };

    bool res = !is_leader(cr)
        && leaderList.size() < maxLeaders // free leader slots
        && d->worksum() > minWork // provides more work
        && d->grid().valid_checkpoint() // valid checkpoint
        && version_ok(cr)
        && (!id || id != d->descriptor); // no signed pin fail for this descriptor

    if (res) {
        assert(d->chain_length() != 0); // ensured by Descripted::valid() because d->worksum() cannot be 0, check res assignment.
    }
    return res;
}

bool Downloader::valid_shared_batch(const SharedBatch& sb)
{
    if (chains.signed_snapshot().has_value()) {
        const auto& ss = chains.signed_snapshot().value();
        for (const auto* ptr { &sb }; ptr->valid(); ptr = &ptr->prev()) {
            if (ptr->upper_height() < ss.height())
                break;
            if (ptr->lower_height() <= ss.height()) {
                if ((*ptr)[ss.height()].hash() != ss.hash)
                    return false;
            }
        }
    }
    return true;
}

bool Downloader::consider_insert_leader(Conref cr, BanList& offenders)
{
    auto pos { leaderList.end() };
    if (!can_insert_leader(cr))
        return false;

    NonzeroSnapshot sn { cr.chain().descripted() };
    auto o = global().pbr->find_last(sn.descripted->grid(), chains.signed_snapshot());
    if (!o)
        return false;
    auto& pin { *o };

    syncdebug_log().info("acquire findLast: [{},{}]", pin.lower_height().value(), pin.upper_height().value());

    if (!valid_shared_batch(pin))
        return false;

    Height bl = cr.chain().stage_fork_range().lower();
    Height cl = cr.chain().consensus_fork_range().lower();
    auto pd { (bl > cl)
            ? ProbeData { cr.chain().stage_fork_range(), chains.stage_pin() }
            : ProbeData { cr.chain().consensus_fork_range(), chains.consensus_pin() } };

    // This node has announced a chain with more total work than have. So it must have headers that we don't know yet,
    // which means that the peer's descripted chain length must be greater than the equal length,
    // i.e. sn.length >= lower(). Otherwise, the node has faked the total work.
    if (sn.length < pd.fork_range().lower()) {
        offenders.insert_unique({ { EFAKEWORK, sn.length }, cr });
        return false;
    }

    Lead_iter li;
    if (pin.valid()) {
        auto vi = acquire_verifier(std::move(pin));
        li = leaderList.emplace(pos, cr, std::move(sn), std::move(pd), vi);
    } else { // no element of g is a shared batch
        li = leaderList.emplace(pos, cr, std::move(sn), std::move(pd));
    }

    data(cr).leaderIter = li;
    queue_requests(li);
    return true;
}

void Downloader::erase_leader(const Lead_iter li)
{
    while (li->queuedIters.size() > 0)
        release_first_queued_batch(li);

    if (li->verifier.has_value())
        release_verifier((*li->verifier));

    data(li->cr).leaderIter = leaderList.end();
    leaderList.erase(li);
}

void Downloader::queue_requests(Lead_iter li)
{
    const auto& d = *li->snapshot.descripted;
    auto ns = li->next_slot();
    auto s = ns + li->queuedIters.size();
    for (; s < d.grid().slot_end() && s < ns + pendingDepth; ++s) {
        if (s.index() == 0)
            acquire_queued_batch({}, d.grid()[s], li);
        else
            acquire_queued_batch(d.grid()[s - 1], d.grid()[s], li);
    }
}

Conref Downloader::try_send(ConnectionFinder& f, BanList& offenders, const ReqData& rd)
{ // OK
    uint32_t index = f.conIndex;
    uint32_t bound = connections.size();
    for (size_t i = 0; i < 2; ++i) {
        for (; index != bound; ++index) {
            Conref cr = connections[index];
            if (cr.busy())
                continue;

            // convenience abbreviations
            const auto& pd = data(cr).probeData;
            auto& desc = cr.chain().descripted();
            const Grid& g = desc->grid();

            // ignore this connection if it header not present
            if (rd.slot >= g.slot_end() || g[rd.slot] != rd.finalHeader)
                continue;
            // At this point g[rd.slot] == rd.finalHeader, i.e. this peer
            // has announced a grid with our desired header at correct position.

            // consider updating probe with cacheMatch
            if (rd.cacheMatch && (!pd || pd->qiter == rd.queueEntry.iter)) {
                ForkRange& fr { rd.cacheMatch->fork_range(cr) };
                try {
                    // The peer's fork range of the stage/consensus chain
                    // (which is selected by the fork_range() method) must match
                    // at height rd.slot.offset() because of the grid announced
                    // by that peer. On error ban it (manipulated grid header info).
                    fr.on_match(rd.slot.offset());
                } catch (const ChainError& e) {
                    offenders.push_back({ e, cr });
                    continue;
                }
                // set or improve probe data
                if (!pd || (pd->fork_range().lower() < fr.lower())) {
                    clear_connection_probe(cr);
                    ProbeData newpd { fr, std::move(rd.cacheMatch->pin) };
                    set_connection_probe(cr, std::move(newpd), desc, rd.queueEntry.iter);
                }
            }

            // consider probe data if applicable
            if (pd && pd->qiter == rd.queueEntry.iter) {
                assert(rd.slot.upper() + 1 > pd->fork_range().lower()); // condition from can_download
                auto br { ProbeBalanced::slot_batch_request(*pd, pd->dsc,
                    rd.slot, rd.finalHeader) };
                if (br) {
                    f.s.send(cr, *br);
                    clear_connection_probe(cr);
                    f.conIndex = index;
                    return cr;
                }
            } else {
                Batchrequest br(desc, rd.slot, rd.finalHeader);
                f.s.send(cr, br);
                f.conIndex = index;
                return cr;
            }
        }
        index = 0;
        bound = f.conIndex;
    }
    return {};
}

bool Downloader::try_final_request(LeaderNode& leader, RequestSender& sender)
{
    // convenience abbreviations
    auto& pd = leader.probeData;
    const NonzeroSnapshot& s = leader.snapshot;
    auto& desc = s.descripted;
    Batchslot descriptedSlot = desc->grid().slot_end();

    // The following is ensured by Descripted::valid, still check it here
    assert(descriptedSlot.upper() > desc->chain_length());
    assert(descriptedSlot.offset() <= desc->chain_length());

    Batchslot focusMaxSlot = leader.next_slot() + pendingDepth;

    if (focusMaxSlot >= descriptedSlot // in reach
        && desc->chain_length().incomplete_batch_size() != 0 // non-empty descripted slot
        && leader.finalBatch.batch.size() == 0) // not yet filled
    {
        // Consider updating probeData with cacheMatch
        if (s.descripted == leader.cr.chain().descripted()) {
            // The saved snapshot (i.e. the chain we are interested to download from this leader) is
            // the leader's latest, i.e. up-to-date advertised chain.
            // For each peer's latest advertised chain, we have the fork range of their chain with respect
            // to both, our consensus chain and our stage chain. We can use this information to improve
            // the probe data fork range lower bound (the height that we know the chains are equal up to).

            auto& sfr = leader.cr.chain().stage_fork_range();
            if (pd.fork_range().lower() < sfr.lower()) {
                // We know that the stage chain is equal to the peers latest advertised chain up to height
                // sfr.lower() - 1. This a longer equal length (better information) than we know from the
                // current probeData `pd`. So we replace the ProbeData by the information we get from
                // stage chain fork range.
                pd = ProbeData { sfr, chains.stage_pin() };
            }

            auto& cfr = leader.cr.chain().consensus_fork_range();
            if (pd.fork_range().lower() < cfr.lower()) {
                // We know that the consensus chain is equal to the peers latest advertised chain up to height
                // sfr.lower() - 1. This a longer equal length (better information) than we know from the
                // current probeData `pd`. So we replace the ProbeData by the information we get from
                // consensus chain fork range.
                pd = ProbeData { cfr, chains.consensus_pin() };
            }
            assert(s.length >= pd.fork_range().lower());
        }

        // Same condition as in can_download
        // A leader by definition must have more total work and more total length than the chains we know
        auto forkRangeLower { pd.fork_range().lower() };
        if (s.length < forkRangeLower) {
            spdlog::error("forkRangeLower = {} > {} = s.length", forkRangeLower.value(), s.length.value() + 1);
            assert(false);
        }

        auto br { ProbeBalanced::final_partial_batch_request(pd, desc, s.length, leader.snapshot.worksum) };
        if (br) {
            sender.send(leader.cr, *br);
            return true;
        }
    }
    return false;
}

void Downloader::do_probe_requests(RequestSender s)
{
    for (auto li : leaderList) {
        if (s.finished())
            break;
        auto cr = li.cr;
        if (cr.busy())
            continue;

        const auto& pd { li.probeData };
        const auto& dsc { li.snapshot.descripted };
        // A leader must have block headers that we have not seen yet, otherwise
        // we would not have selected it as a leader. So the chain length is greater
        // than the highest equal length (lower() -1):
        Height chainLength { dsc->chain_length().nonzero_assert() };
        assert(chainLength + 1 > pd.fork_range().lower());
        auto pr { ProbeBalanced::probe_request(pd, dsc, chainLength.nonzero_assert()) };
        if (pr)
            s.send(cr, *pr);
    }
    for (auto cr : connectionsWithProbeJob) {
        if (s.finished())
            break;
        if (cr.busy())
            continue;
        assert(data(cr).probeData);
        const auto& pd { *data(cr).probeData };

        // We want upper slot height as maxLength because complete batches are shared
        // automatically, such that probe requests can only succeed within that batch
        auto maxLength { Batchslot(pd.fork_range().lower()).upper() };
        assert(maxLength + 1 > pd.fork_range().lower()); // condition from can_download
        auto pr { ProbeBalanced::probe_request(pd, pd.dsc, maxLength) };
        if (pr)
            s.send(cr, *pr);
    }
}

// final requests are requests for partial header batches which are
// not complete. They can only be handled by the leader itself, i.e.
// are exclusive, because we cannot be sure other nodes have exactly
// the same chain (especially last block).
bool Downloader::do_exclusive_final_requests(RequestSender& s)
{
    for (Conref cr : connections) {
        if (s.finished())
            return true;
        if (!cr.busy() && is_leader(cr))
            try_final_request(*data(cr).leaderIter, s);
    }
    return s.finished();
}

// grid requests are requests for a complete batch that is identified
// by a final hash (last hash in the batch) and saved in the grid
// of chain hashes transmitted to us by each node.
// Many connections can be used to retrieve the header batch.
BanList Downloader::do_shared_grid_requests(RequestSender& s)
{
    BanList res;
    ConnectionFinder cf(s, connections);
    for (auto& ln : leaderList) {
        for (auto& q : ln.queued()) {
            if (s.finished())
                return res;
            if (q.node().has_pending_request())
                continue;
            ReqData rd(q);
            // A solo batch is a batch which can only be found from the leader
            // i.e. no other node has advertised a chain which we know must
            // contain this batch.
            if (q.is_solo()) {
                // specifically deal with solo batches:
                // cache for later request pins
                rd.cacheMatch = chains.lookup(q.pin_prev());
            }
            if (Conref cr = try_send(cf, res, rd); cr.valid()) {
                q.node().cr = cr;
                data(cr).jobPtr = &q.node();
            }
        }
    }
    return res;
}

BanList Downloader::do_header_requests(RequestSender s)
{
    // highest priority for exclusive requests
    // to prevent these connections being busy with
    // other requests
    if (do_exclusive_final_requests(s))
        return {};
    return do_shared_grid_requests(s);
}

void Downloader::on_request_expire(Conref cr, const Batchrequest&)
{
    if (data(cr).jobPtr) {
        data(cr).jobPtr->cr.clear();
        data(cr).jobPtr = nullptr;
    }
}

void Downloader::on_proberep(Conref c, const Proberequest& req, const ProberepMsg& rep)
{
    if (!rep.requested)
        return;
    auto& dat { data(c) };

    // match pin
    if (dat.probeData) {
        auto& pin { *dat.probeData };
        if (pin.dsc->descriptor == req.descriptor)
            pin.match(req.height, *rep.requested);
    }

    // match leader info
    if (is_leader(c)) {
        auto li = data(c).leaderIter;
        if (li->snapshot.descripted->descriptor == req.descriptor)
            li->probeData.match(req.height, *rep.requested);
    }
}

void Downloader::on_probe_request_expire(Conref /*cr*/)
{
    // do nothing
}

void Downloader::process_final(Lead_iter li, std::vector<Offender>& out)
{
    const auto& b = li->finalBatch.batch;

    if (li->final_slot() != li->next_slot())
        return;
    if (li->finalBatch.batch.size() == 0) {
        if (li->snapshot.length.incomplete_batch_size() == 0) {
            assert(li->verified_total_work() > minWork);
        }
        return;
    }
    bool fromGenesis = !li->verifier.has_value();
    HeaderRange hrange { li->final_slot(), b };

    const HeaderVerifier parent {
        [&] {
            if (auto hv { chains.header_verifier(hrange) }; hv.has_value())
                return *hv;
            return fromGenesis ? HeaderVerifier {} : (*li->verifier)->second.verifier;
        }()
    };

    auto o { parent.copy_apply(chains.signed_snapshot(), hrange.sub_range(parent.height() + 1)) };
    if (!o.has_value()) {
        out.push_back({ o.error(), li->cr });
        return;
    }

    // compute worksum
    Worksum worksum = li->verified_total_work() + li->final_batch_work();

    // check for fake work
    if (worksum < li->finalBatch.claimedWork) {
        if (config().localDebug) {
            assert(0 == 1); // There should be no bad actor during local debug
        }
        out.push_back({ { EFAKEWORK, (parent.height() + 1).nonzero_assert() }, li->cr });
        return;
    }

    // update maximizer
    if (!maximizer.has_value() || std::get<2>(maximizer.value()) < worksum) {
        auto sb = (fromGenesis ? SharedBatch {} : (*li->verifier)->second.sb);
        maximizer = { { li->cr, li->snapshot.descripted }, HeaderchainSkeleton(std::move(sb), b), worksum };
    }
}

bool Downloader::advance_verifier(const Ver_iter* vi, const Lead_set& leaders, const Batch& b,
    std::vector<Offender>& out)
{

    auto a {
        (vi ? (*vi)->second.verifier : HeaderVerifier {})
            .copy_apply(chains.signed_snapshot(), HeaderRange((vi ? (*vi)->second.sb.next_slot() : Batchslot(0)), b))
    };
    if (!a.has_value()) {
        for (const Lead_iter& li : leaders) {
            out.push_back({ a.error(), li->cr });
        }
        return false;
    }
    HeaderVerifier& hv { a.value() };
    auto sharedBatch { global().pbr->share(Batch { b }, (vi ? (*vi)->second.sb : SharedBatch {})) };

    // update maximizer
    Worksum worksum = sharedBatch.total_work();
    if (!maximizer.has_value() || std::get<2>(maximizer.value()) < worksum) {
        Lead_iter li = *leaders.begin();
        maximizer = { { li->cr, li->snapshot.descripted }, HeaderchainSkeleton(sharedBatch, {}), worksum };
    }

    auto p = verifierMap.try_emplace(b.last(), std::move(sharedBatch), std::move(hv));
    assert(p.second);
    auto vi_new = p.first;
    std::map<Queued_iter, Lead_set> qmap;
    for (auto& li : leaders) {
        li->verifier = vi_new;
        vi_new->second.refcount += 1;
        if (vi)
            release_verifier(*vi);
        release_first_queued_batch(li);
        queue_requests(li);
        if (li->queuedIters.size() > 0) {
            auto qi = li->queuedIters.front().iter;
            if (qi->second.batch.complete())
                qmap[qi].insert(li);
        } else {
            process_final(li, out);
        }
    }
    for (auto& [qi, leaders] : qmap) {
        verify_queued(qi, leaders, out);
    }
    return true;
}

BanList Downloader::filter_leadermismatch_offenders(std::vector<Offender> chainOffenders)
{
    BanList res;
    for (auto [co, cr] : chainOffenders) {
        if (co.e == ELEADERMISMATCH) {
            auto& d { data(cr) };
            if (is_leader(cr)) {
                d.ignoreDescriptor = d.leaderIter->snapshot.descripted->descriptor;
                erase_leader(d.leaderIter);
            }
        } else {
            res.push_back(co);
        }
    }
    return res;
}

void Downloader::verify_queued(Queued_iter qi, const Lead_set& leaders, std::vector<Offender>& offenders)
{
    auto& queued = qi->second;

    bool action = false;
    std::map<Ver_iter, Lead_set> next;
    Lead_set tmpFromGenesis;
    for (const Lead_iter& li : leaders) {
        assert(li->queuedIters.size() > 0);
        if (li->queuedIters.front().iter == qi) {
            action = true;
            if (li->verifier) {
                next[*li->verifier].insert(li);
            } else {
                tmpFromGenesis.insert(li);
            }
        }
    }

    uint32_t succeeded = 0;
    if (tmpFromGenesis.size() > 0) {
        if (advance_verifier(nullptr, tmpFromGenesis, queued.batch, offenders))
            succeeded += 1;
    }
    for (auto& [vi, leaders] : next) {
        if (advance_verifier(&vi, leaders, queued.batch, offenders))
            succeeded += 1;
    }
    if (action) {
        // only header chain including this header batch can be correct (up to hash
        // collision), others must be bad header chains
        assert(succeeded <= 1);
    } else {
        assert(succeeded == 0);
    }
}

auto Downloader::on_response(Conref cr, Batchrequest&& req, Batch&& res) -> BanList
{
    // assert precondition
    assert(res.size() >= req.minReturn);
    assert(res.size() <= req.max_return());

    // safety check
    const bool withJobIter = (data(cr).jobPtr != nullptr);
    if (withJobIter) {
        data(cr).jobPtr->cr.clear();
        data(cr).jobPtr = nullptr;
        if (req.is_partial_request())
            spdlog::error("BUG in {}:{}: safety check failed.", __FILE__, __LINE__);
    }

    const Batchslot batchSlot { Height { req.selector.startHeight } };

    auto minWorkSnapshot = minWork;
    std::vector<Offender> offenders;
    req.prefix.append(res);
    Batch& b(req.prefix);

    if (req.is_partial_request()) {
        if (!is_leader(cr))
            return {};
        auto li = data(cr).leaderIter;
        auto& d_old = *li->snapshot.descripted;
        if (req.selector.descriptor != d_old.descriptor
            || batchSlot != li->final_slot())
            return {};
        li->finalBatch = { std::move(b), std::get<Worksum>(req.extra) };

        if (li->next_slot() == batchSlot) {
            process_final(li, offenders);
        }
    } else {
        auto qi = queuedBatches.find(std::get<Header>(req.extra));
        if (qi != queuedBatches.end() && cr == qi->second.cr) {
            if (!withJobIter)
                spdlog::error("BUG in {}:{}: withJobIter==false.", __FILE__, __LINE__);
            qi->second.cr.clear();
        }

        if (!b.complete())
            return { ChainOffender { ChainError(EBATCHSIZE2, req.selector.startHeight), cr } };
        if (qi == queuedBatches.end())
            return {};
        auto& queued = qi->second;
        if (queued.batch.complete())
            return {};
        queued.batch = std::move(b);
        queued.originId = cr.id();

        verify_queued(qi, queued.leaderRefs, offenders);
    }
    auto ret { filter_leadermismatch_offenders(std::move(offenders)) };
    if (minWorkSnapshot != minWork) {
        prune_leaders();
    }
    select_leaders(ret);
    return ret;
}

[[nodiscard]] std::optional<std::tuple<LeaderInfo, Headerchain, BanList>> Downloader::pop_data()
{
    if (!has_data()) {
        return {};
    }
    auto& val = maximizer.value();
    LeaderInfo& li = std::get<0>(val);
    Headerchain chain { std::get<1>(val) };
    assert(chain.total_work() == std::get<2>(val));
    auto banList { set_min_worksum(chain.total_work()) };
    return std::tuple { li, chain, banList };
}

bool Downloader::has_data() const
{
    return maximizer.has_value() && std::get<2>(maximizer.value()) > minWork;
}

auto Downloader::erase(Conref cr) -> std::optional<BanList>
{
    bool erased = std::erase(connections, cr);
    if (!erased)
        return {};

    BanList banList;
    clear_connection_probe(cr);
    if (maximizer.has_value() && std::get<0>(maximizer.value()).cr == cr)
        maximizer.reset();
    const auto& leaderIter = data(cr).leaderIter;
    if (leaderIter != leaderList.end()) {
        erase_leader(leaderIter);
        select_leaders(banList);
    }
    if (data(cr).jobPtr) {
        data(cr).jobPtr->cr.clear();
        data(cr).jobPtr = nullptr;
    }
    return banList;
}

void Downloader::select_leaders(BanList& banList)
{
    if (leaderList.size() >= maxLeaders)
        return;
    for (auto cr : connections) {
        if (consider_insert_leader(cr, banList)
            && leaderList.size() >= maxLeaders)
            return;
    }
}

BanList Downloader::insert(Conref cr)
{
    connections.push_back(cr);
    BanList bl;
    consider_insert_leader(cr, bl);
    return bl;
}

BanList Downloader::set_min_worksum(const Worksum& ws)
{
    BanList banList;
    if (minWork != ws) {
        spdlog::debug("Set downloader minWork = {}", ws.getdouble());
        minWork = ws;
        prune_leaders();
        select_leaders(banList);
    }
    return banList;
}

void Downloader::prune_leaders()
{
    Worksum threshold { minWork };
    if (maximizer.has_value()) {
        Worksum& w = std::get<2>(maximizer.value());
        if (w > threshold) {
            threshold = w;
        }
    }
    for (auto li = leaderList.begin(); li != leaderList.end();) {
        const auto& s = li->snapshot;
        if (s.worksum <= minWork) {
            erase_leader(li++);
        } else {
            ++li;
        }
    }
}

BanList Downloader::on_append(Conref cr)
{
    BanList out;
    consider_insert_leader(cr, out);
    return out;
}

BanList Downloader::on_fork(Conref cr)
{
    BanList out;
    consider_insert_leader(cr, out);
    return out;
}

BanList Downloader::on_rollback(Conref c)
{
    if (is_leader(c))
        erase_leader(data(c).leaderIter);

    BanList out;
    consider_insert_leader(c, out);
    return out;
}

BanList Downloader::on_signed_snapshot_update()
{
    if (maximizer.has_value()) {
        // verify
        auto hc { std::get<1>(*maximizer) };
        if (!chains.signed_snapshot()->compatible_inefficient(hc)) {
            maximizer.reset();
        };
    }
    prune_leaders();
    BanList out;
    select_leaders(out);
    return out;
}

}

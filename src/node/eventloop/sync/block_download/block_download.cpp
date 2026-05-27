#include "block_download.hpp"
#include "../sync.hpp"
#include "attorney.hpp"
#include "block/body/view.hpp"
#include "block/chain/binary_forksearch.hpp"
#include "chainserver/server.hpp"
#include "eventloop/address_manager/address_manager_impl.hpp"
#include "eventloop/chain_cache.hpp"
#include "eventloop/eventloop.hpp"
#include "eventloop/types/peer_requests.hpp"
#include "spdlog/spdlog.h"

namespace BlockDownload {
using enum ServerCall;

const Headerchain& Downloader::headers() const
{
    return attorney.headers();
}

auto Downloader::connections()
{
    return attorney.connections();
}

Downloader::Downloader(Attorney attorney, size_t windowLength)
    : attorney(attorney)
    , focus(*this, windowLength)
{
}

std::vector<ChainOffender> Downloader::handle_stage_result(stage_operation::StageAddResult&& a)
{
    auto offenders { stageState.on_result(a) };
    if (a.ce)
        reset();
    return offenders;
}

std::vector<ChainOffender> Downloader::handle_stage_result(stage_operation::StageSetResult&& r)
{
    auto optionalAdvance { stageState.on_result(r) };
    if (optionalAdvance) // no error
        focus.set_offset(*optionalAdvance - 1);
    else // error due to signedSnapshot
        reset();
    return {}; // no possible offenders for stage set
}

ServerCall Downloader::next_stage_call() // OK
{
    if (initialized == false || stageState.pendingOperation)
        return NO;
    if (stageState.is_stage_set_phase()) {
        if (stageState.stageSetAck < headers().length())
            return STAGE_SET;
    } else {
        if (focus.has_data())
            return STAGE_ADD;
    }
    return NO;
}

stage_operation::StageAddOperation Downloader::pop_stage_add() // OK
{
    assert(next_stage_call() == STAGE_ADD);

    stageState.pendingOperation.set_stage_add(
        forks.lower_bound(focus.height_begin()),
        forks.end());
    return { headers(), focus.pop_data() };
}

stage_operation::StageSetOperation Downloader::pop_stage_set() // OK
{
    assert(next_stage_call() == STAGE_SET);
    assert(stageState.is_stage_set_phase()); //
    stageState.pendingOperation.set_stage_set(headers().length());
    return { headers() };
}

std::optional<Height> Downloader::reachable_length()
{
    return forks.reachable_length();
}

void Downloader::check_upgrade_descripted(Conref c)
{
    auto& fdata = data(c);
    assert(fdata.descripted().use_count() > 0);
    if (c->chain.descripted() == fdata.descripted())
        return;
    auto l1 = fdata.fork_range().lower();
    auto l2 = c->chain.stage_fork_range().lower();
    if (l1 <= l2) {
        forks.pin_current_chain(c);
    }
}

void Downloader::on_fork(Conref c)
{
    if (!initialized)
        return;
    check_upgrade_descripted(c);
}

void Downloader::on_append(Conref c)
{
    if (!initialized)
        return;
    check_upgrade_descripted(c);
}

void Downloader::on_rollback(Conref)
{
    // new chain must be shorter, no check_upgrade_descripted(c);
}

void Downloader::on_probe_reply(Conref c, const ProbereqMsg& req, const ProberepMsg& rep)
{
    if (!initialized)
        return;
    auto& fdata = data(c);
    if (req.descriptor != fdata.descripted()->descriptor) {
        return;
    }
    assert((*fdata.descripted()).chain_length() >= req.height);

    if (!rep.requested.has_value()) { // chain info no longer available at peer
        forks.pin_current_chain(c);
        return;
    }
    forks.match(c, headers(), req.height, *rep.requested);
}

std::vector<ChainOffender> Downloader::init(std::tuple<HeaderDownload::LeaderInfo, Headerchain, BanList> thc) // OK?
{
    assert(reachableWork <= headers().total_work());

    auto& [li, hc, out] = thc;

    // spdlog::info("Syncing from {}", li.cr->c->peer_endpoint().to_string());
    ForkHeight fh = attorney.set_stage_headers(std::move(hc));

    assert(headers().length() != 0);

    if (stageState.pendingOperation.busy()) {
        stageState.set_stale_from(fh.val());
    } else if (fh.forked() && fh.val() < focus.height_begin()) {
        stageState.clear_non_pending();
        spdlog::debug("INIT: start_set_phase");
    } else {
        spdlog::debug("INIT: NO start set phase forked: {}", fh.forked());
        if (fh.forked()) {
            spdlog::debug("      fh: {} >= {} : focus.height_begin()", fh.val().value(), focus.height_begin().value());
        }
    }

    initialized = true;
    auto validLeader { false };

    ///////////
    // apply fork info
    forks.clear();
    for (auto c : connections()) {
        try {
            if (fh.forked())
                c->chain.on_stage_fork(fh.val(), headers());
            else
                c->chain.on_stage_append_or_shrink(headers());
        } catch (ChainError e) {
            focus.erase(c);
            out.push_back({ e, c });
            continue;
        }

        if (c == li.cr) {
            ForkRange fr((headers().length() + 1).nonzero_assert());
            forks.pin_leader_chain(c, li.descripted, fr);
            validLeader = true;
        } else {
            forks.pin_current_chain(c);
        }
    }

    assert(validLeader);

    focus.fork(fh);
    update_reachable(true);
    return std::move(out);
}

void Downloader::insert(Conref c) // OK
{
    if (!initialized)
        return;
    forks.pin_current_chain(c);
    update_reachable();
}

void Downloader::do_probe_requests(RequestSender rs)
{
    if (!initialized)
        return;
    Height focusBegin { focus.height_begin() };
    for (auto c : connections()) {
        if (rs.finished())
            return;
        if (c.job())
            continue;
        if (!data(c).has_fork_data()) {
            spdlog::error("Peer {} has_fork_data == false", c->c->peer_address().to_string());
        }
        const auto& fr { data(c).fork_range() };
        auto a { data(c).fork_iter()->first };
        auto b { fr.lower() };
        if (a != b) {
            spdlog::error("data(c).fork_iter()->first={}, fr.lower()={}", a.value(), b.value());
        }
        assert(data(c).fork_iter()->first == fr.lower()); // assert failed for me!!
        NonzeroHeight u {
            [&]() -> NonzeroHeight {
                if (fr.forked())
                    return fr.upper();
                auto l1 { headers().length() };
                auto l2 { data(c).descripted()->chain_length() };
                return (std::min(l1, l2) + 1).nonzero_assert();
            }()
        };
        if (u > focusBegin) {
            assert(u >= fr.lower());
            auto probeHeight { fr.lower() + (u - fr.lower()) / 2 };
            if (probeHeight > fr.lower()) {
                Proberequest req(data(c).descripted(), probeHeight);
                rs.send(c, req);
            }
        }
    }
}

bool Downloader::can_do_requests()
{
    return initialized
        && !stageState.is_stage_set_phase()
        && reachable_length() >= focus.height_begin();
}

void Downloader::do_block_requests(RequestSender s) // OK?
{
    if (!can_do_requests() || s.finished())
        return;

    assert(reachable_length() <= headers().length());
    assert(!stageState.pendingOperation.is_stage_set());

    BlockSlot downloadSlot(focus.height_begin());
    Height minHeight { std::min(downloadSlot.upper_height(), headers().length()) };
    auto forkIter = forks.lower_bound((minHeight + 1).nonzero_assert());
    if (forkIter == forks.end())
        return;
    for (auto n : focus) {
        if (!n.has_value() || n->iter->second.activeRequest())
            continue;

        // found request
        auto& range { n->r };
        if (forkIter->first <= range.upper)
            forkIter = forks.lower_bound(range.upper + 1);
        while (true) {
            if (forkIter == forks.end())
                return;
            if (!forkIter->second.job()) {
                Conref& cr = forkIter->second;
                auto req { n->link_request(cr) };
                s.send(cr, req);
                if (s.finished())
                    return;
                break;
            }
            ++forkIter;
        }
    }
}

std::optional<stage_operation::Operation> Downloader::pop_stage()
{
    switch (next_stage_call()) {
    case STAGE_ADD:
        return pop_stage_add();
    case STAGE_SET:
        return pop_stage_set();
    case NO:
    default:
        return {};
    }
}

void Downloader::on_blockreq_reply(Conref cr, BlockrepMsg&& rep, Blockrequest& req)
{ // OK
    focus.erase(cr);

    if (!initialized)
        return;

    if (rep.empty()) {
        if (!req.descripted->expired()) {
            throw ChainError { EEMPTY, req.range.lower };
        } else {
            return;
        }
    }

    // check for failed requests
    if (rep.blocks.size() == 0) {
        throw Error(EEMPTY);
    }

    // check for correct length
    if (rep.blocks.size() != req.range.length())
        throw Error(EINV_BLOCKREPSIZE);

    // discard old replies
    if (req.range.upper < focus.height_begin())
        return;

    // check hash
    if (headers().length() < req.range.upper)
        return;
    if (headers().hash_at(req.range.upper) != req.upperHash)
        return;

    // check merkle roots
    size_t i0 = (req.range.lower < focus.height_begin() ? focus.height_begin() - req.range.lower : 0);
    for (size_t i = i0; i < rep.blocks.size(); ++i) {
        auto height { req.range.lower + i };
        BodyView bv(rep.blocks[i].view(height));
        if (!bv.valid())
            throw Error(EINV_BODY);
        if (bv.merkle_root(height) != headers()[height].merkleroot())
            throw Error(EMROOT);
    }

    const BlockSlot slot(req.range.lower);
    focus.set_blocks(slot, req.range.lower, std::move(rep.blocks));
    return;
}

void Downloader::reset()
{
    attorney.clear_blockdownload();
    assert(headers().length() == 0);

    // download target related
    reachableWork.setzero();
    reachableHeight = Height(0);
    forks.clear();

    // download focus related
    focus.clear();

    // state helper variables
    initialized = false;
    stageState.clear();
}

bool Downloader::erase(Conref cr)
{ // OK
    forks.erase(cr);
    focus.erase(cr);
    if (!initialized) 
        return false;
    return update_reachable();
}

void Downloader::on_blockreq_expire(Conref cr)
{ // OK
    focus.erase(cr);
}

void Downloader::on_probe_expire(Conref)
{
    // do nothing
}

std::vector<ChainOffender> Downloader::on_stage_result(stage_operation::Result&& r)
{
    return std::visit([&]<typename T>(T&& r) {
        return handle_stage_result(std::forward<T>(r));
    },
        std::move(r));
}

bool Downloader::update_reachable(bool reset)
{ // OK
    if (forks.size() == 0) {
        initialized = false;
        stageState.clear();
        reachableWork.setzero();
        reachableHeight = Height(0);
        return true;
    } else {
        auto iter = forks.rbegin();
        Height fh = iter->first;
        assert(fh != 0);
        if (reset || fh - 1 != reachableHeight) {
            Worksum tmp = headers().total_work_at(fh - 1);
            assert(tmp != reachableWork);
            reachableWork = tmp;
            reachableHeight = fh - 1;
            return true;
        }
        return false;
    }
}

void Downloader::set_min_worksum(const Worksum& ws)
{
    if (!initialized)
        return;
    if (ws >= headers().total_work()) {
        spdlog::debug("Disable blockdownload");
        initialized = false;
        stageState.clear();
    }
}
}

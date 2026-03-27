#include "subscription_state.hpp"
#include "api/events/subscription.hpp"
#include "api/types/shared.hpp"
#include "chainserver/state/state.hpp"
#include "chainserver/state/update/update.hpp"
#include "spdlog/spdlog.h"
#include "wrt/optional.hpp"
#include <ranges>

namespace {
void pre_check_nonzero(ssize_t modified, ssize_t& nonzero)
{
    if (modified == 0)
        nonzero += 1;
}
void post_check_nozero(ssize_t modified, ssize_t& nonzero)
{
    if (modified == 0)
        nonzero -= 1;
}
}

namespace chain_subscription {
auto ChainSubscriptionState::chain_state(const chainserver::State& s)
{
    auto v { std::move(s.api_get_latest_blocks(num_latest_blocks).blocks_reversed) };
    std::reverse(v.begin(), v.end());
    return subscription::events::Event {
        subscription::events::ChainState {
            .head { s.api_get_head() },
            .latestBlocks { std::move(v) } }
    };
}
void ChainSubscriptionState::handle_subscription(SubscriptionRequest&& r, const chainserver::State& s)
{
    using enum subscription::Action;
    switch (r.action) {
    case Unsubscribe:
        subscriptions.erase(r.sptr.get());
        break;
    case Subscribe:
        subscriptions.insert(r.sptr);
        chain_state(s).send(std::move(r.sptr));
    }
}

size_t ChainSubscriptionState::erase_all(subscription_data_ptr r)
{
    return subscriptions.erase(r) ? 1 : 0;
}

void ChainSubscriptionState::on_chain_changed(const chainserver::State& state, const subscription_state::NewBlockInfo& nbi)
{
    // rollback api actions
    if (auto s { nbi.rollback }) {
        auto& l { s->length };
        auto v { state.api_get_latest_blocks(num_latest_blocks).blocks_reversed };
        std::reverse(v.begin(), v.end());
        subscription::events::Event {
            subscription::events::ChainFork {
                .head { state.api_get_head() },
                .latestBlocks { std::move(v) },
                .rollbackLength { l } }
        }.send(subscriptions.entries());
    } else {
        if (nbi.newBlocks.size() > num_latest_blocks) {
            chain_state(state).send(subscriptions.entries());
        } else {
            subscription::events::Event {
                subscription::events::ChainAppend {
                    .head { state.api_get_head() },
                    .newBlocks { nbi.newBlocks } }
            }.send(subscriptions.entries());
        }
    }
}
}

namespace minerdist_subscriptions {
void MinerdistSubscriptionState::handle_subscription(SubscriptionRequest&& r, const chainserver::State& s)
{
    using enum subscription::Action;
    switch (r.action) {
    case Unsubscribe:
        erase(r.sptr.get());
        break;
    case Subscribe:
        if (subscriptions.insert(r.sptr))
            fill_aggregator(s);
        aggregator.value().state_event().send(std::move(r.sptr));
    }
}
void MinerdistSubscriptionState::erase(subscription_data_ptr p)
{
    subscriptions.erase(p);
    if (subscriptions.size() == 0)
        aggregator.reset();
}

void MinerdistSubscriptionState::on_chain_changed(const chainserver::State& a, const subscription_state::NewBlockInfo& nbi)
{
    if (!aggregator)
        return;
    auto& blocks { nbi.newBlocks };
    if (blocks.size() > nMiners) {
        aggregator->clear();
        for (size_t i = blocks.size() - nMiners; i < blocks.size(); ++i) {
            aggregator->push_back(blocks[i].reward().transaction.data.toAddress);
        }
    } else {
        if (nbi.rollback)
            aggregator->rollback(nbi.rollback->distance);
        for (auto& b : blocks)
            aggregator->push_back(b.reward().transaction.data.toAddress);
        fill_aggregator(a);
        aggregator->truncate(nMiners);
    }
    aggregator.value().summarize().send(subscriptions.entries());
}

void MinerdistSubscriptionState::fill_aggregator(const chainserver::State& s)
{
    auto& a { aggregator };
    bool created = false;
    if (!a.has_value()) {
        a.emplace();
        created = true;
    }
    if (a->size() >= nMiners || s.chainlength() == 0)
        return;
    auto u { s.chainlength().nonzero_assert().subtract_clamp1(a->size()) };
    auto miners { s.api_get_miners(u.latest(nMiners - a->size())) };
    for (auto& miner : std::ranges::reverse_view(miners)) {
        a->push_front(miner.address, !created);
    };
}

void Aggregator::clear()
{
    *this = {};
}

subscription::events::Event Aggregator::summarize()
{
    bool useDelta { nonzeroDelta < nonzeroCount };
    std::vector<api::AddressCount> counts;
    for (auto iter { map.begin() }; iter != map.end();) {
        auto& [k, v] { *iter };
        if (useDelta) {
            if (v.delta != 0)
                counts.push_back({ k, v.delta });
        } else {
            if (v.count != 0) {
                counts.push_back({ k, v.count });
            }
        }
        v.delta = 0;
        if (v.count == 0)
            map.erase(iter++);
        else
            ++iter;
    }
    nonzeroDelta = 0;
    if (useDelta)
        return { subscription::events::MinerdistDelta { std::move(counts) } };
    return { subscription::events::MinerdistState { std::move(counts) } };
}

subscription::events::Event Aggregator::state_event()
{
    std::vector<api::AddressCount> counts;
    for (auto& [k, v] : map) {
        if (v.count != 0)
            counts.push_back({ k, v.count });
    }
    return { subscription::events::MinerdistState { std::move(counts) } };
}

void Aggregator::push_back(const Address& a)
{
    auto iter { map.try_emplace(a).first };
    iters.push_back(iter);
    count_new_iter(iter, true);
}

void Aggregator::push_front(const Address& a, bool trackDelta)
{
    auto iter { map.try_emplace(a).first };
    iters.push_front(iter);
    count_new_iter(iter, trackDelta);
}

void Aggregator::rollback(size_t by)
{
    for (size_t i { 0 }; i < by; ++i) {
        if (size() == 0)
            break;
        uncount_iter(iters.back());
        iters.pop_back();
    }
}

void Aggregator::truncate(size_t maxlen)
{
    while (iters.size() > maxlen) {
        uncount_iter(iters.front());
        iters.pop_front();
    }
}

void Aggregator::uncount_iter(iter_t iter)
{

    // don't need pre_check as count is >=0
    iter->second.count -= 1;
    post_check_nozero(iter->second.count, nonzeroCount);

    pre_check_nonzero(iter->second.count, nonzeroDelta);
    iter->second.delta -= 1;
    post_check_nozero(iter->second.count, nonzeroDelta);
}

void Aggregator::count_new_iter(iter_t iter, bool trackDelta)
{
    pre_check_nonzero(iter->second.count, nonzeroCount);
    iter->second.count += 1;
    // don't need post check as count is >=0

    if (trackDelta) {
        pre_check_nonzero(iter->second.count, nonzeroDelta);
        iter->second.delta += 1;
        post_check_nozero(iter->second.count, nonzeroDelta);
    }
}

}
namespace address_subscription {
wrt::optional<SessionAddressCursor> SessionData::session_cursor(const api::Block& b)
{
    if (forceReload)
        return {};
    if (++i > 100) {
        blocks.clear();
        forceReload = true;
        return {};
    }
    if (blocks.size() == 0 || blocks.back().height < b.height) {

        blocks.push_back(b);
    }
    return SessionAddressCursor { blocks.back() };
}

auto AddressSubscriptionState::account_state(const Address& a, const chainserver::State& s)
{
    return subscription::events::Event {
        subscription::events::AccountState {
            .address { a },
            .history { s.api_get_history(a) },
        }
    };
}

auto& AddressSubscriptionState::get_session_data(MapVal& mapVal, const Address& a)
{
    if (mapVal.sessionId != sessionId) {
        mapVal.sessionId = sessionId;
        mapVal.sessionMapIter = sessionMap.try_emplace(a).first;
    };
    return mapVal.sessionMapIter->second;
}

void AddressSubscriptionState::handle_subscription(SubscriptionRequest&& r, const chainserver::State& s, const Address& a)
{
    using enum subscription::Action;
    switch (r.action) {
    case Unsubscribe:
        erase(r.sptr, a);
        break;
    case Subscribe:
        insert(r.sptr, a);
        account_state(a, s).send(std::move(r.sptr));
    }
}
bool AddressSubscriptionState::insert(const subscription_ptr& sptr, const Address& a)
{
    auto p { map.try_emplace(a, valIndexCounter) };
    auto newAddress { p.second };
    auto& mapVal { p.first->second };
    auto id { mapVal.id };
    if (!newAddress) {
        for (auto& [id2, sptr2, addr] : subscriptions) {
            if (id2 == id && sptr2.get() == sptr.get())
                return false;
        }
    }
    mapVal.counter += 1;
    subscriptions.push_back({ id, sptr, a });
    return true;
}

size_t AddressSubscriptionState::erase_all(subscription_data_ptr r)
{
    auto n { std::erase_if(subscriptions, [&](elem_t& e) {
        auto& [id, sptr, addr] = e;
        if (sptr.get() == r) {
            auto iter = map.find(addr);
            assert(iter != map.end());
            assert(iter->second.counter > 0);
            iter->second.counter -= 1;
            if (iter->second.counter == 0)
                map.erase(iter);
            return true;
        }
        return false;
    }) };

    return n;
}

size_t AddressSubscriptionState::erase(const subscription_ptr& sptr, const Address& addr)
{
    auto iter { map.find(addr) };
    if (iter == map.end())
        return 0;
    const auto id { iter->second.id };
    return std::erase_if(subscriptions, [&](elem_t& e) {
        auto& [id2, sptr2, addr2] = e;
        if (id == id2 && sptr.get() == sptr2.get()) {
            assert(iter->second.counter > 0);
            iter->second.counter -= 1;
            if (iter->second.counter == 0)
                map.erase(iter);
            return true;
        }
        return false;
    });
}

void AddressSubscriptionState::on_chain_changed(const chainserver::State& state, const subscription_state::NewBlockInfo& nbi)
{
    void session_start();
    if (nbi.rollback)
        session_rollback(nbi.rollback->length);
    for (auto& b : nbi.newBlocks)
        session_block(b);
    session_end(state);
}

void AddressSubscriptionState::session_end(const chainserver::State& s)
{
    for (auto& [address, data] : sessionMap) {
        auto iter = map.find(address);
        assert(iter != map.end());
        auto subscriptions { select_id(iter->second.id) };
        if (data.forceReload) {
            account_state(address, s).send(std::move(subscriptions));
        } else {
            assert(data.blocks.size() > 0);
            subscription::events::AccountDelta ad {
                .address { address },
                .newBalance { s.api_get_wart_balance(address).balance },
                .newTransactions { std::move(data.blocks) },
            };
            subscription::events::Event {
                std::move(ad)
            }
                .send(std::move(subscriptions));
        }
    }
    sessionMap.clear();
}

// session functions
void AddressSubscriptionState::session_start()
{
    sessionId += 1;
    sessionMap.clear();
}
void AddressSubscriptionState::session_rollback(Height h)
{
    for (auto& [addr, mapval] : map) {
        if (h < mapval.latestTransactionHeight) {
            get_session_data(mapval, addr).forceReload = true;
        }
    }
}
void AddressSubscriptionState::session_block(const api::Block& b)
{
    if (auto& r { b.actions.reward }) {
        if (auto c { session_address_cursor(b, r->transaction.data.toAddress, b.height) })
            c->b.set_reward(*r);
    }
    for (auto& t : b.actions.wartTransfers) {
        if (auto c { session_address_cursor(b, t.transaction.data.toAddress, b.height) })
            c->b.actions.wartTransfers.push_back(t);
        if (auto c { session_address_cursor(b, t.transaction.signedData.originAddress, b.height) })
            c->b.actions.wartTransfers.push_back(t);
    }
}
wrt::optional<SessionAddressCursor> AddressSubscriptionState::session_address_cursor(const api::Block& b, const Address& a, Height h)
{
    auto iter { map.find(a) };
    if (iter == map.end())
        return {};
    auto& mapVal { iter->second };
    mapVal.latestTransactionHeight = h;

    return get_session_data(mapVal, a).session_cursor(b);
}

std::vector<subscription_ptr> AddressSubscriptionState::select_id(id_t id)
{
    std::vector<subscription_ptr> res;
    for (auto& [id2, ptr, addr] : subscriptions) {
        if (id == id2) {
            res.push_back(ptr);
        }
    }
    return res;
}
}

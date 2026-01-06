#pragma once
#include "api/types/all.hpp"
#include "block/chain/height.hpp"
#include "block/chain/worksum.hpp"
#include "crypto/address.hpp"
#include "general/logger/log_entry.hpp"
#include "subscription_fwd.hpp"

// struct Subscription
namespace subscription {
void handleSubscriptioinMessage(const nlohmann::json&, subscription_ptr);
namespace events {
//////////////////////////////
/// Account events
struct AccountState {
    static constexpr auto eventName { "account.state" };
    Address address;
    wrt::optional<api::AccountHistory> history;
};

struct AccountDelta {
    static constexpr auto eventName { "account.delta" };
    Address address;
    api::WartBalance newBalance;
    std::vector<api::Block> newTransactions;
};

//////////////////////////////
/// Connection events
struct Connection {
    uint64_t id;
    StartTimePoints since;
    std::string peerAddr;
    bool inbound;
};
struct ConnectionsState {
    static constexpr auto eventName { "connection.state" };
    std::vector<Connection> connections;
    size_t total;
};
struct ConnectionsRemove {
    static constexpr auto eventName { "connection.remove" };
    uint64_t id;
    size_t total;
};
struct ConnectionsAdd {
    static constexpr auto eventName { "connection.add" };
    Connection connection;
    size_t total;
};

//////////////////////////////
/// Chain events
struct ChainState {
    static constexpr auto eventName { "chain.state" };
    api::ChainHead head;
    std::vector<api::Block> latestBlocks;
};
struct ChainAppend {
    static constexpr auto eventName { "chain.append" };
    api::ChainHead head;
    std::vector<api::CompleteBlock> newBlocks;
};
struct ChainFork {
    static constexpr auto eventName { "chain.fork" };
    api::ChainHead head;
    std::vector<api::Block> latestBlocks;
    Height rollbackLength;
};
struct MinerdistState {
    static constexpr auto eventName { "minerdist.state" };
    std::vector<api::AddressCount> counts;
};
struct MinerdistDelta {
    static constexpr auto eventName { "minerdist.delta" };
    std::vector<api::AddressCount> deltas;
};
struct LogState {
    static constexpr auto eventName { "log.state" };
    std::vector<LogEntry> lines;
};
struct LogLine {
    static constexpr auto eventName { "log.line" };
    LogEntry line;
};
struct Event {
    using variant_t = std::variant<
        AccountState,
        AccountDelta,
        ConnectionsState,
        ConnectionsRemove,
        ConnectionsAdd,
        ChainState,
        ChainAppend,
        ChainFork,
        MinerdistState,
        MinerdistDelta,
        LogState,
        LogLine>;
    std::string json_str() const;
    void send(std::vector<subscription_ptr>) &&;
    void send(subscription_ptr) &&;
    variant_t variant;
};
}

// struct RequestParams;
// namespace topics {
//     // void emit_connect(size_t total, const ConnectionBase& c);
//     // void emit_disconnect(size_t total, uint64_t id);
//     // void emit_chain_state(OnChainEvent);
//     // void emit_mempool_add(const mempool::Put&, size_t total);
//     // void emit_mempool_erase(const mempool::Erase&, size_t total);
//     // void emit_rollback(Height h);
//     // void emit_block_append(api::Block&&);
//
//     struct Account {
//         static constexpr auto subscriptionTopic { "account" };
//         Account(RequestParams);
//         Address address;
//     };
//     struct Chain {
//         static constexpr auto subscriptionTopic { "chain" };
//     };
//     struct Connections {
//         static constexpr auto subscriptionTopic { "connections" };
//     };
//
//     template <typename T>
//     struct Unsubscribe {
//     };
//
//     template <typename T>
//     struct Subscribe : public T {
//         using T::T;
//     };
//     template<typename T>
//     struct subscription_type;
//
//     template<typename T>
//     struct subscription_type<Subscribe<T>>{
//         using type = T;
//     };
//
//     template<typename T>
//     struct subscription_type<Unsubscribe<T>>{
//         using type = T;
//     };
//     template<typename T>
//     using subscription_type_v=subscription_type<T>::type;
//
//     template <typename... Types>
//     struct variant_builder;
//
//     template <typename A>
//     struct variant_builder<A> {
//         template <typename... B>
//         using type = std::variant<Unsubscribe<A>, Subscribe<A>, B...>;
//     };
//
//     template <typename A, typename... Rest>
//     struct variant_builder<A, Rest...> {
//         template <typename... B>
//         using type = variant_builder<Rest...>::template type<Unsubscribe<A>, Subscribe<A>, B...>;
//     };
//     using helper_t = variant_builder<Account, Chain, Connections>;
//     using variant_t = helper_t::type<>;
//
//     ;
// };
// struct Request {
//     using variant_t = topics::variant_t;
//
//     auto visit(auto lambda) const
//     {
//         return std::visit(lambda, variant);
//     }
//     auto visit(auto lambda)
//     {
//         return std::visit(lambda, variant);
//     }
//
//     Request(std::string_view s)
//         : Request(parse(s))
//     {
//     }
//
// private:
//     Request(variant_t v)
//         : variant(std::move(v))
//     {
//     }
//     static Request parse(std::string_view);
//
// private:
//     variant_t variant;
// };
}

// struct SubscriptionAction {
//     bool subscribe;
//     std::string topic;
// };

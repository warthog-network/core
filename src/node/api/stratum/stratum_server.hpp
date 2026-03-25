#pragma once
#ifndef DISABLE_LIBUV
#include "chainserver/mining_subscription.hpp"
#include "communication/mining_task.hpp"
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace uvw {
class loop;
class async_handle;
class tcp_handle;
};
class StratumServer;
namespace stratum {
namespace msg {
    struct MiningSubscribe;
    struct MiningAuthorize;
    struct MiningSubmit;
}

class Connection : public std::enable_shared_from_this<Connection> {
    struct Writer {
        Connection& c;
        template <typename T>
        Writer& operator<<(T&& t)
        {
            c.write_line(t.to_string());
            return *this;
        }
    };
    friend class ::StratumServer;
    friend struct Writer;

public:
    Connection(std::shared_ptr<uvw::tcp_handle> newHandle, StratumServer& server);

    void on_message(std::string_view msg);
    ~Connection();

    void send_result(int64_t stratumId, Error);

private:
    void handle_message(msg::MiningSubscribe&& s);
    void handle_message(msg::MiningSubmit&& m);
    void handle_message(msg::MiningAuthorize&& m);
    void send_work(std::string jobId, const Block& block, bool clean);
    void shutdown();
    void write_line(const std::string& line);
    void process_line();
    Writer write() { return { *this }; }

    struct Authorized {
        Address address;
        std::string worker;
    };
    bool fresh { true };
    const std::array<uint8_t, 4> extra2prefix;
    wrt::optional<Authorized> authorized;
    std::string stratumLine;
    std::shared_ptr<uvw::tcp_handle> handle;
    StratumServer& server;
};

}

class StratumServer {
    friend class stratum::Connection;
    struct AddressData {
        using Subscription = mining_subscription::MiningSubscription;
        bool clean { true };
        std::vector<stratum::Connection*> connections;
        Subscription subscription;
        AddressData(std::function<Subscription()>);
        void clear()
        {
            blocks.clear();
            clean = true;
        }
        Block* find_block(const std::string& jobId);
        Block* add_block(const std::string& jobId, Block&& b);

    private:
        std::map<std::string, Block> blocks;
    };
    struct SubscriptionFeed {
        Address address;
        ChainMiningTask t;
    };
    struct ShutdownEvent {
    };
    struct AppendResult {
        std::weak_ptr<stratum::Connection> p;
        int64_t stratumId;
        Error result;
    };
    using Event = std::variant<SubscriptionFeed, ShutdownEvent, AppendResult>;
    void push(Event e);
    void handle_events();
    void handle_event(SubscriptionFeed&&);
    void handle_event(ShutdownEvent&&);
    void handle_event(AppendResult&&);

    void acceptor(TCPPeeraddr endpointAddresss);
    void link_authorized(const Address&, stratum::Connection*);
    void unlink_authorized(const Address&, stratum::Connection*);

    wrt::optional<Block> get_block(Address, std::string jobId);

public:
    StratumServer(TCPPeeraddr endpointAddress);
    ~StratumServer();
    void start();
    void shutdown();
    void request_mining();
    void on_mining_task(Address, ChainMiningTask&&);
    void on_append_result(AppendResult);

private:
    std::map<Address, AddressData> addressData;
    std::list<std::shared_ptr<stratum::Connection>> connections;
    const std::shared_ptr<uvw::loop> loop;

    std::thread worker;
    std::mutex m;
    std::vector<Event> events;
    const std::shared_ptr<uvw::async_handle> async;
};
#endif /* ifndef  */

#pragma once
#include "api/interface.hpp"
#include "health_fwd.hpp"
#include "wrt/variant.hpp"
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace api {
struct HealthState {
    std::vector<std::string> messages;
};
}

class Health {
private:
    std::thread t;
    bool finished { false };
    std::condition_variable cv;
    mutable std::mutex m;
    std::map<std::string, std::string, std::less<void>> map;
    using get_ret_t = std::vector<std::ranges::range_value_t<decltype(map)>>;

    // events
    struct ShutdownEv {
    };
    struct SetEv {
        std::string key;
        std::string message;
        bool overwrite;
    };
    struct RemoveEv {
        std::string key;
    };
    using Variant = wrt::variant<ShutdownEv, SetEv, RemoveEv, HealthCb>;
    std::vector<Variant> queue;

    void push_ev(Variant v)
    {
        std::lock_guard l(m);
        queue.push_back(std::move(v));
    }
    void handle_event(ShutdownEv)
    {
        finished = true;
    }
    void handle_event(SetEv&&);
    void handle_event(RemoveEv&&);
    void handle_event(HealthCb&&) const;

public:
    Health()
    {
        t = std::thread([&] { run(); });
    }
    ~Health()
    {
        push_ev(ShutdownEv {});
        t.join();
    }
    void run();
    void set(std::string key, std::string message, bool overwrite = true)
    {
        push_ev(SetEv { std::move(key), std::move(message), overwrite });
    }
    void remove(std::string key)
    {
        push_ev(RemoveEv(std::move(key)));
    }
    auto get(HealthCb cb)
    {
        push_ev(std::move(cb));
    }
};

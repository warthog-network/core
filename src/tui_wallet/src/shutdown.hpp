#pragma once
#include <functional>
#include <mutex>
#include <optional>
#include <string>

struct ShutdownState {

    bool trigger(const std::string& msg = "")
    {
        std::lock_guard l(m);
        if (state.has_value())
            return false;
        state = msg;
        call_all();
        return true;
    }
    void add_callback(std::function<void()> f)
    {
        shutdownCallbacks.push_back(std::move(f));
    }
    void apply(auto lambda) const
    {
        std::lock_guard l(m);
        lambda(state);
    }

private:
    void call_all()
    {
        for (auto& f : shutdownCallbacks)
            f();
    }

private:
    mutable std::mutex m;
    std::vector<std::function<void()>> shutdownCallbacks;
    std::optional<std::string> state;
};

inline ShutdownState shutdownState;

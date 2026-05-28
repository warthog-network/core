#pragma once
#include "data/retrieval_context.hpp"
#include "shutdown.hpp"
#include "spdlog/spdlog.h"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <thread>
class TaskThread {
private:
    std::jthread t;
    using job_t = std::function<void()>;
    using job_ptr = std::unique_ptr<job_t>;
    job_ptr nextJob;
    bool active;
    bool shutdown_ { false };
    std::mutex m;
    std::condition_variable cv;
    void work()
    {
        try {
            while (true) {
                job_ptr currentJob;
                {
                    std::unique_lock l(m);
                    active = false;
                    cv.wait(l, [&]() { return shutdown_ || nextJob; });
                    active = true;
                    if (shutdown_)
                        return;
                    currentJob = std::move(nextJob);
                }
                (*currentJob)();
            }
        } catch (std::exception& e) {
            shutdownState.trigger(e.what());
        }
    }
    void shutdown()
    {
        std::lock_guard l(m);
        shutdown_ = true;
        cv.notify_all();
    }

public:
    void set_job(std::function<void()> job, bool force = true)
    {
        std::lock_guard l(m);
        if (active && !force)
            return;
        nextJob = std::make_unique<std::function<void()>>(std::move(job));
        cv.notify_all();
    }
    TaskThread()
    {
        t = std::jthread([&]() { work(); });
    }
    ~TaskThread()
    {
        shutdown();
    }
};
template <typename T>
struct UpdatableValue {
    size_t nonce { 0 };
    std::chrono::steady_clock::time_point expires;
    std::optional<T> value;
    TaskThread taskThread;
    void set_pending()
    {
        expires = std::chrono::steady_clock::time_point::max();
        value.reset();
        nonce += 1;
    }
};
struct StateUpdaterContext {
    DataRetrievalContext& retrievalContext;
};

template <typename... Ts>
class DataStateUpdater : public std::enable_shared_from_this<DataStateUpdater<Ts...>> {

    static constexpr auto expirationInterval { std::chrono::seconds(5) };

    template <typename T, typename... Args>
    requires(std::is_same_v<T, Ts> || ...)
    void start_update(const StateUpdaterContext& ctx, UpdatableValue<T>& owned, auto defer, auto on_complete, Args&&... args)
    {
        using sc = std::chrono::steady_clock;
        // start update process
        // set to max while update job is pending
        owned.set_pending();
        auto w { std::enable_shared_from_this<DataStateUpdater<Ts...>>::weak_from_this() };
        owned.taskThread.set_job(T::get_data(ctx.retrievalContext, [w = std::move(w), &owned, n = owned.nonce, defer = std::move(defer), on_complete = std::move(on_complete)](std::optional<T> v) { defer([w = std::move(w), v = std::move(v), on_complete = std::move(on_complete), &owned, n]() {
                                                                                                                                                                                                         if (auto s { w.lock() }) {
                                                                                                                                                                                                             if (n == owned.nonce) {
                                                                                                                                                                                                                 owned.expires = sc::now() + expirationInterval;
                                                                                                                                                                                                                 owned.value = std::move(v);
                                                                                                                                                                                                                 on_complete();
                                                                                                                                                                                                             } else {
                                                                                                                                                                                                                 std::cerr << "owned nonce not equal: n== " << n << " owned.nonce==" << owned.nonce << std::endl;
                                                                                                                                                                                                             }
                                                                                                                                                                                                         }
                                                                                                                                                                                                     }); }, std::forward<Args>(args)...));
    }

public:
    template <typename T, typename... Args>
    requires(std::is_same_v<T, Ts> || ...)
    const auto& get(const StateUpdaterContext& ctx, bool clearCache, auto defer, auto on_complete, Args&&... args)
    {
        using sc = std::chrono::steady_clock;
        auto& o { std::get<UpdatableValue<T>>(tuple) };
        if (clearCache || o.expires <= sc::now()) {
            start_update<T>(ctx, o, std::move(defer), std::move(on_complete), std::forward<Args>(args)...);
        }
        return o.value;
    }

private:
    std::tuple<UpdatableValue<Ts>...> tuple;
};

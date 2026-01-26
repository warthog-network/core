#pragma once
#include "data/retrieval_context.hpp"
#include <chrono>
#include <memory>
#include <thread>

template <typename... Ts>
class DataStateUpdater : public std::enable_shared_from_this<DataStateUpdater<Ts...>> {
    template <typename T>
    struct Owned {
        size_t nonce { 0 };
        std::chrono::steady_clock::time_point expires;
        std::optional<T> value;
    };
    struct Threads {
    private:
        std::vector<std::unique_ptr<std::jthread>> threads;

    public:
        Threads() { };
        Threads(const Threads&) = delete;
        Threads(Threads&&) = default;
        void prune()
        {
            std::erase_if(threads, [](const std::unique_ptr<std::jthread>& t) {
                return t->joinable();
            });
        }
        void push_back(std::jthread t)
        {
            threads.push_back(std::make_unique<std::jthread>(std::move(t)));
        }
        ~Threads()
        {
        }
    } threads;

    template <typename T, typename... Args>
    requires(std::is_same_v<T, Ts> || ...)
    void start_update(Owned<T>& owned, auto defer, auto on_complete, Args&&... args)
    {
        using sc = std::chrono::steady_clock;
        // start update process
        // set to max while update job is pending
        owned.expires = std::chrono::steady_clock::time_point::max();
        // reset value because it expired
        owned.value.reset();

        owned.nonce += 1;
        threads.prune();
        auto w { std::enable_shared_from_this<DataStateUpdater<Ts...>>::weak_from_this() };
        threads.push_back(T::get_data(retrievalContext, [w = std::move(w), &owned, n = owned.nonce, defer = std::move(defer), on_complete = std::move(on_complete)](std::optional<T> v) {
            defer([w = std::move(w), v = std::move(v), on_complete = std::move(on_complete), &owned, n](){
                constexpr auto expirationInterval { std::chrono::seconds(5) };

                if (auto s{w.lock()}){
                    if (n == owned.nonce) {
                        owned.expires = sc::now() + expirationInterval;
                        owned.value = std::move(v);
                        on_complete();
                    }
                }
            }); }, std::forward<Args>(args)...));
    }

    template <typename T, typename... Args>
    requires(std::is_same_v<T, Ts> || ...)
    void try_update(bool force, Args&&... args)
    {
        using sc = std::chrono::steady_clock;
        auto& o { std::get<Owned<T>>(tuple) };
        if (force || o.expires <= sc::now()) {
            start_update<T>(o, std::forward<Args>(args)...);
        }
    }

public:
    template <typename T, typename... Args>
    requires(std::is_same_v<T, Ts> || ...)
    const auto& get(bool clearCache, auto defer, auto on_complete, Args&&... args)
    {
        try_update<T>(clearCache, std::move(defer), std::move(on_complete), std::forward<Args>(args)...);
        return std::get<Owned<T>>(tuple).value;
    }
    DataStateUpdater(DataRetrievalContext retrievalContext)
        : retrievalContext(std::move(retrievalContext))
    {
    }

    DataRetrievalContext retrievalContext;

private:
    std::tuple<Owned<Ts>...> tuple;
};

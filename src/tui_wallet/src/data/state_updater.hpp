#pragma once

#include "data/retrieval_context.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

template <typename... Ts>
class DataStateUpdater {
    template <typename T>
    struct Entry {
        mutable std::mutex m;
        std::chrono::steady_clock::time_point expires;
        std::jthread t;
        std::optional<T> value;
    };

    template <typename T>
    using TupleEntry = std::unique_ptr<Entry<T>>;
    template <typename T>
    requires(std::is_same_v<T, Ts> || ...)
    void try_update(auto&& on_complete)
    {
        using sc = std::chrono::steady_clock;
        Entry<T>& r { *std::get<TupleEntry<T>>(tuple) };
        std::lock_guard l(r.m);
        if (r.expires <= sc::now()) {
            // start update process
            // set to max while update job is pending
            r.expires = std::chrono::steady_clock::time_point::max();
            // reset value because it expired
            r.value.reset();
            if (r.t.joinable())
                r.t.join();
            r.t = T::get_data(retrievalContext, [&r, on_complete = std::forward<decltype(on_complete)>(on_complete)](std::optional<T> v) {
                constexpr auto expirationInterval { std::chrono::seconds(5) };
                std::lock_guard l(r.m);
                r.expires = sc::now() + expirationInterval;
                r.value = std::move(v);
                std::cerr << "r.value.has_value()" << r.value.has_value() << std::endl;
                on_complete();
            });
        }
    }

public:
    template <typename T>
    requires(std::is_same_v<T, Ts> || ...)
    auto get(auto on_complete)
    {
        try_update<T>(std::move(on_complete));
        const Entry<T>& r { *std::get<TupleEntry<T>>(tuple) };
        std::lock_guard l(r.m);
        std::cerr << "2: r.value.has_value()" << r.value.has_value() << std::endl;
        return r.value;
    }
    DataStateUpdater(DataRetrievalContext retrievalContext)
        : retrievalContext(std::move(retrievalContext))
        , tuple(std::make_unique<Entry<Ts>>()...)
    {
    }

    DataRetrievalContext retrievalContext;

private:
    std::tuple<TupleEntry<Ts>...> tuple;
};

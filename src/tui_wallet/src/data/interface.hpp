#pragma once
#include "api/types/shared.hpp"
#include "data/state_updater.hpp"

struct WartBalance : public api::FundsBalance {
    static std::jthread get_data(const DataRetrievalContext& ctx, auto callback)
    {
        return std::jthread([&ctx, callback = std::move(callback)]() {
            WartBalance bal { ctx.get_wart_balance() };
            callback(std::optional<WartBalance> { bal });
        });
    }
};

struct DataInterface : public DataStateUpdater<WartBalance> {
    auto get_wart_balance(auto onComplete)
    {
        return get<WartBalance>(std::move(onComplete));
    }
};

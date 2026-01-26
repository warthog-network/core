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

struct TokenCompletion : public api_types::TokenList {
    static std::jthread get_data(const DataRetrievalContext& ctx, auto callback, std::string namePrefix, std::string hashPrefix)
    {
        return std::jthread([&ctx, callback = std::move(callback), namePrefix = std::move(namePrefix), hashPrefix = std::move(hashPrefix)]() {
            auto bal { ctx.endpoint.token_complete(namePrefix, hashPrefix) };
            callback(std::optional<TokenCompletion>(bal));
        });
    }
};

struct DataInterface {
private:
    using updater_t = DataStateUpdater<WartBalance, TokenCompletion>;
    using defer_t = std::function<void(std::function<void()>)>;
    std::shared_ptr<updater_t> updater;
    defer_t defer;

public:
    DataInterface(DataRetrievalContext init, defer_t defer)
        : updater(std::make_shared<updater_t>(init))
        , defer(std::move(defer))
    {
    }
    auto& retrieval_context() const{return updater->retrievalContext;}
    [[nodiscard]] auto& get_wart_balance(auto onComplete)
    {
        return updater->get<WartBalance>(false, defer, std::move(onComplete));
    }
    [[nodiscard]] auto& token_complete(bool clearCache, auto onComplete, std::string namePrefix, std::string hashPrefix)
    {
        return updater->get<TokenCompletion>(clearCache, defer, std::move(onComplete), namePrefix, hashPrefix);
    }
};

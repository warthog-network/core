#pragma once

#include "block/body/transaction_types.hpp"
#include "general/fwd.hpp"
#include "wrt/type_collection.hpp"
#include <cstddef>
#include <type_traits>
#include <utility>

template <typename... Ts>
class TypemapBool {
    template <size_t... Is>
    void for_each(auto&& lambda, std::index_sequence<Is...>)
    {
        (FWD(lambda)((Ts*)(nullptr), arr[Is]), ...);
    }

public:
    template <typename T>
    requires(std::is_same_v<Ts, T> || ...)
    bool& at()
    {
        size_t i = 0;
        bool* b;
        ([&]() -> bool {
            if constexpr (std::is_same_v<Ts, T>) {
                b = &arr[i];
                return true;
            }
            i += 1;
            return false;
        }() || ...);
        return *b;
    }
    void for_each(auto&& lambda)
    {
        for_each(FWD(lambda), std::index_sequence_for<Ts...>());
    }
    TypemapBool() { arr.fill(false); }
    TypemapBool(auto&& lambda)
        : arr { FWD(lambda)((Ts*)(nullptr))... }
    {
    }

private:
    std::array<bool, sizeof...(Ts)> arr;
};

template <typename T>
class TransactionMapTemplate;
template <typename... Ts>
class TransactionMapTemplate<wrt::TypeCollection<Ts...>> : public TypemapBool<Ts...> {
    using TypemapBool<Ts...>::TypemapBool;
};

struct TransactionMapBool : public TransactionMapTemplate<TransactionTypes> {
    using TransactionMapTemplate::TransactionMapTemplate;
};

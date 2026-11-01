#pragma once
#include "general/fwd.hpp"
#include <map>
namespace wrt {
template <typename K, typename V>
struct map : public std::map<K, V> {
    using std::map<K, V>::map;
    auto try_emplace_lambda(const K& k, auto&& generator)
    {
        struct {
            std::remove_cvref_t<decltype(generator)> gen;
            operator V() const { return gen(); }
        } g { FWD(generator) };
        return std::map<K, V>::try_emplace(k, g);
    }
};
}

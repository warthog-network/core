#pragma once
#include "block/chain/height.hpp"
#include "txmap.hpp"
#include <type_traits>

namespace mempool {
struct ComparatorPin {
    using const_iter_t = Txmap::const_iterator;
    using is_transparent = std::true_type;
    inline bool operator()(const_iter_t i1, Height h2) const
    {
        return (i1->first.pinHeight < h2);
    }
    inline bool operator()(const_iter_t i1, const_iter_t i2) const
    {
        if (i1->first.pinHeight == i2->first.pinHeight)
            return i1->first < i2->first;
        return (i1->first.pinHeight < i2->first.pinHeight);
    }
};
struct ComparatorFee {
    using const_iter_t = Txmap::const_iterator;
    inline bool operator()(const_iter_t i1, const_iter_t i2) const
    {
        if (i1->second.fee == i2->second.fee) {
            return i1->first < i2->first;
        };
        return i1->second.fee > i2->second.fee;
    }
};
struct ComparatorHash {
    using const_iter_t = Txmap::const_iterator;
    using is_transparent = std::true_type;
    inline bool operator()(const_iter_t i1, const_iter_t i2) const
    {
         if(i1->second.hash == i2->second.hash) // tie breaker for duplicate hash
            return i1->first < i2->first;
        return i1->second.hash < i2->second.hash;
    }
    inline bool operator()(const_iter_t i1, const HashView rhs) const
    {
        return i1->second.hash < rhs;
    }
    inline bool operator()(HashView lhs, const_iter_t i2) const
    {
        return lhs < i2->second.hash;
    }
};
}

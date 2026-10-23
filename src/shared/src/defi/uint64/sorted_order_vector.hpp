#pragma once
#include "types.hpp"
#include <algorithm>
#include <vector>

namespace defi {
struct SortedOrderVector_uint64 {
    struct elem_t : public Order_uint64 {
        elem_t(Order_uint64 o)
            : Order_uint64(std::move(o))
        {
        }

        auto operator<=>(const elem_t& o) const
        {
            return limit.operator<=>(o.limit);
        };
        auto operator==(const elem_t& o) const { return limit == o.limit; }
    };

    size_t size() const { return v.size(); }
    auto& operator[](size_t i) const { return v[i]; }
    SortedOrderVector_uint64() { }
    bool delete_at(size_t i)
    {
        if (i < size()) {
            total.subtract_assert(v[i].amount);
            v.erase(v.begin() + i);
            return true;
        }
        return false;
    }
    void insert_smallest_desc(Order_uint64 o)
    {
        if (size() > 0)
            assert(o.limit < v.back().limit);
        v.push_back(o);
        total.add_assert(o.amount);
    }

    void insert_desc(Order_uint64 o)
    {
        auto iter { std::lower_bound(v.rbegin(), v.rend(), o) };
        if (iter != v.rend() && *iter == o)
            iter->amount.add_assert(o.amount);
        else
            v.insert(iter.base(), std::move(o));
        total.add_assert(o.amount);
    }
    void insert_largest_asc(Order_uint64 o)
    {
        if (size() > 0)
            assert(o.limit > v.back().limit);
        v.push_back(o);
        total.add_assert(o.amount);
    }
    void insert_asc(Order_uint64 o)
    {
        auto iter { std::lower_bound(v.begin(), v.end(), o) };
        if (iter != v.end() && *iter == o)
            iter->amount.add_assert(o.amount);
        else
            v.insert(iter, std::move(o));
        total.add_assert(o.amount);
    }
    auto total_push() const { return total; }

private:
    std::vector<elem_t> v;
    Funds_uint64 total { Funds_uint64::zero() };
};
}

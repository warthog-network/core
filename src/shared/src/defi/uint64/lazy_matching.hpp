#pragma once

#include "orderbook.hpp"
#include "pool.hpp"

namespace defi {

// lazy matching i.e. buy and sell orders are loaded on demand
[[nodiscard]] inline MatchResult_uint64 match_lazy(auto& loaderSellAsc, auto& loaderBuyDesc, const PoolLiquidity_uint64& poolBeforeMatch)
{
    std::optional<Price_uint64> pr { loaderBuyDesc.next_price() };
    if (!pr)
        pr = loaderSellAsc.next_price();
    if (!pr)
        return { .toPool {}, .filled { 0, 0 } }; // no orders, so no action
    auto price { *pr };

    Orderbook_uint64 ob;
    std::optional<Price_uint64> lower, upper;
    BaseQuote_uint64 filled { 0, 0 };

    // load sell orders with price <= `price`
    std::optional<Price_uint64> p;
    while (auto np { loaderSellAsc.next_price() }) {
        assert(p < *np); // prices must be strictly increasing
        p = np;
        if (*p > price) {
            upper = *p;
            break;
        }
        Order_uint64 o { loaderSellAsc.load_next_order() };
        ob.insert_largest_base(o);
        lower = o.limit; // tighten `lower` around `price`
        filled.base.add_assert(o.amount);
    }
    size_t I { ob.base_asc_sell().size() }; // sell index bound

    // load buy orders with price > `price`
    // now require strictness to avoid selecting degenerate (zero-length) section
    p.reset();
    while (auto np { loaderBuyDesc.next_price() }) {
        assert(!p || *p > *np); // prices must be strictly decreasing
        p = np;
        if (*p <= price) {
            if (lower < *p)
                lower = *p;
            break;
        }
        Order_uint64 o { loaderBuyDesc.load_next_order() };
        ob.insert_smallest_quote(o);
        if (!upper || *upper > o.limit)
            upper = o.limit; // tighten `upper` around `price`
        filled.quote.add_assert(o.amount);
    }
    size_t J { ob.quote_desc_buy().size() }; // buy index bound

    assert(!upper || upper != lower); // we cannot have degenerate (zero-length) section

    // lambda to check whether at current price we need more quote/less base
    // for equilibrium
    auto more_quote_less_base = [&](Price_uint64 p) {
        Delta_uint64 toPool { filled.excess(p) };
        return !poolBeforeMatch.modified_pool_price_exceeds(toPool, p);
    };

    if (upper && !more_quote_less_base(*upper)) {
        while (std::optional<Price_uint64> np { loaderSellAsc.next_price() }) {
            while (J != 0) {
                auto& b { ob.quote_desc_buy()[J - 1] };
                if (b.limit > *np)
                    break;
                filled.quote.subtract_assert(b.amount);
                J -= 1;
            }
            if (more_quote_less_base(*np))
                break;
            Order_uint64 o { loaderSellAsc.load_next_order() };
            assert(*np == o.limit);
            ob.insert_largest_base(o);
            filled.base.add_assert(o.amount.value());
        }
    } else if (lower && more_quote_less_base(*lower)) {
        while (std::optional<Price_uint64> np { loaderBuyDesc.next_price() }) {
            while (I != 0) {
                auto& b { ob.base_asc_sell()[I - 1] };
                if (b.limit <= *np)
                    break;
                filled.base.subtract_assert(b.amount);
                I -= 1;
            }
            if (!more_quote_less_base(*np))
                break;
            Order_uint64 o { loaderBuyDesc.load_next_order() };
            assert(*np == o.limit);
            ob.insert_smallest_quote(o);
            filled.quote.add_assert(o.amount.value());
        }
    }

    return ob.match(poolBeforeMatch);
}
}

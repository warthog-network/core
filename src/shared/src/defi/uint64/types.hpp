#pragma once
#include "general/funds.hpp"
#include "match_result.hpp"
#include "price.hpp"

namespace defi {

struct Order_uint64 {
    Order_uint64(Reader& r)
        : amount(r)
        , limit(r) { };
    Order_uint64(Funds_uint64 amount, Price_uint64 limit)
        : amount(std::move(amount))
        , limit(std::move(limit)) { };
    Funds_uint64 amount;
    Price_uint64 limit;
};

struct BaseQuote_uint64 : public BaseQuote64 {
    using BaseQuote64::BaseQuote64;
    BaseQuote_uint64(BaseQuote64 bq)
        : BaseQuote64(std::move(bq))
    {
    }

    // Computes excess at specific price rounded towards 0.
    // For exactly no excess, isQuote = true
    Delta_uint64 excess(Price_uint64 p) const
    {
        auto q { multiply_ceil(base.value(), p) };
        if (q.has_value() && *q <= quote) {
            // ceil(base*p) <= quote implies base*p <= quote because quote is an integer
            // => We have too much quote
            return { true, diff_assert(quote, Funds_uint64::from_value_throw(*q)) };
        }
        // Here we know ceil(base * p) > quote but `quote` is an integer, so base * p > quote.
        // This implies so p != 0 and quote / p < base.

        auto b { divide_ceil(quote.value(), p) };
        assert(b.has_value()); // cannot overflow since quote / p < base
        assert(*b <= base); // ceil(quote / p) <= base because quote / p < base and base is an integer.
        return { false, diff_assert(base, Funds_uint64::from_value_throw(*b)) };
    }
    auto price() const { return PriceRelative_uint64::from_fraction(quote.value(), base.value()); }
};

inline BaseQuote_uint64 Delta_uint64::base_quote() const
{
    if (isQuote)
        return { 0, amount };
    return { amount, 0 };
}
}

#include "matcher.hpp"
#include <numeric>
#include <variant>

namespace defi {
std::optional<NonzeroDelta_uint64> FilledAndPool::balance_pool_interaction() const
{
    if (pool.quote.is_zero() || pool.base.is_zero())
        return {}; // don't use pool if pool is degenerated

    struct ret_t {
        std::strong_ordering rel;
        using PoolRatio128 = Ratio128;
        struct Fill64Ratio {
            uint64_t numerator, denominator;
        };
        bool exceeded() const { return std::holds_alternative<PoolRatio128>(v); }
        auto& get_unexceeded_ratio() const { return std::get<Fill64Ratio>(v); };
        auto& get_exceeded_ratio() const { return std::get<PoolRatio128>(v); };
        std::variant<Ratio128, Fill64Ratio> v;
    };

    auto nondecreasing_relation_base = [&](uint64_t baseToPool) -> ret_t {
        // Pool price after we swap `baseToPool` from base to quote (sell) at the pool
        auto poolRatio { pool.price_ratio_added_base(baseToPool) };

        // If we subtract `baseToPool` from `in.base`,
        // new in price is in_numerator/in_denominator
        auto in_numerator { in.quote.value() };
        auto in_denominator { in.base.value() - baseToPool };

        // To compare the prices we compare these products
        auto pool_price_score { poolRatio.numerator * in_denominator };
        auto in_price_score { poolRatio.denominator * in_numerator };

        // As we push more base from in to pool (i.e. sell), the pool price will at some point
        // be smaller than the in price, to make the relation nondecreasing (less -> equal -> greater)
        // in the argument `baseToPool` we compare in_price_score <=> pool_price_score.
        auto rel { in_price_score <=> pool_price_score };

        // The second return argument is for the tie braker [TB] below in the `bisect` lambda
        // and shall balance the two converged bounds of different relation.
        // We will pick the one that maximizes (because maximizing the value minimizes the distance
        // to the original price as the price decreases as we swap more base, i.e. sell, so the
        // orignal price is higher)
        // the minimum of in_price and pool_price. We save info about this minimum in second argument.
        // We swap denominator and numerator here to avoid case distinction tie braker
        // (we need to "maximize min price" for nondecreasing_relation_base but "minimize max price"
        // for nondecreasing_releation_quote)
        if (rel == std::strong_ordering::greater) // fill price > pool price
            return { rel, ret_t::PoolRatio128 { poolRatio.denominator, poolRatio.numerator } };
        else // fill price <= pool price
            return { rel, ret_t::Fill64Ratio { in_denominator, in_numerator } };
    };

    auto nondecreasing_releation_quote = [&](uint64_t quoteToPool) -> ret_t {
        // Pool price after we swap `quoteToPool` from quote to base (buy) at the pool
        auto poolRatio { pool.price_ratio_added_quote(quoteToPool) };

        // If we subtract `quoteToPool` from `in.quote`,
        // new in price is `in_numerator / in_denominator`.
        auto in_numerator { in.quote.value() - quoteToPool };
        auto in_denominator { in.base.value() };

        // To compare the prices we compare these products:
        auto pool_price_score { poolRatio.numerator * in_denominator };
        auto in_price_score { poolRatio.denominator * in_numerator };

        // As we push more quote from in to pool (i.e. buy), the pool price will at some point
        // be greater than the in price. To make the relation nondecreasing (less -> equal -> greater)
        // in the argument `quoteToPool` we compare pool_price_score <=> in_price_score.
        auto rel { pool_price_score <=> in_price_score };

        // The second return argument is for the tie braker [TB] below in the `bisect` lambda
        // and shall balance the two converged bounds of different relation.
        // We will pick the one that minimizes (because minimizing the value minimizes the distance
        // to the original price as the price increases as we swap more quote, i.e. buy, so the
        // original price is smaller)
        // the maximum of in_price and pool_price. We save info about this minimum in second argument.
        // In nondecreasing_relation_base we swap denominator and numerator but here we don't. This is
        // to avoid case distinction tie braker
        // (we need to "maximize min price" for nondecreasing_relation_base but "minimize max price"
        // for nondecreasing_releation_quote)
        if (rel == std::strong_ordering::greater) // pool price < fill price
            return { rel, ret_t::PoolRatio128 { poolRatio } };
        else // fill price >= pool price
            return { rel, ret_t::Fill64Ratio { in_numerator, in_denominator } };
    };

    auto bisect = [](ret_t::Fill64Ratio ratio0, uint64_t v1,
                      auto asc_fun) {
        if (v1 == 0)
            return v1;
        ret_t r { asc_fun(v1) };
        assert(r.rel != std::strong_ordering::less);
        if (r.rel == std::strong_ordering::equal)
            return v1;
        auto ratio1 { r.get_exceeded_ratio() };
        uint64_t v0 { 0 };
        while (true) {
            uint64_t v { std::midpoint(v0, v1) };
            if (v0 == v)
                break;
            ret_t ret = asc_fun(v);
            if (ret.exceeded()) {
                v1 = v;
                ratio1 = ret.get_exceeded_ratio();
            } else {
                v0 = v;
                ratio0 = ret.get_unexceeded_ratio();
            }
        }

        // Tie braker [TB]
        // See comments in nondecreasing_releation_quote and
        // nondecreasing_relation_base. We want to minimize the
        // maximum distance of the original pool price and both,
        // the new pool price and the fill price
        if (ratio1.denominator * ratio0.numerator < ratio1.numerator * ratio0.denominator)
            return v0; // ratio0 < ratio1
        return v1; // ratio0 >= ratio1
    };
    auto baseRet { nondecreasing_relation_base(0) };
    auto quoteRet { nondecreasing_releation_quote(0) };
    auto make_toPool = [&](bool isQuote,
                           Funds_uint64 toPool) -> std::optional<NonzeroDelta_uint64> {
        if (auto nz { toPool.nonzero() }) {
            return NonzeroDelta_uint64 { isQuote, *nz };
        }
        return {};
    };
    if (baseRet.rel == std::strong_ordering::greater) {
        // need to push quote to pool
        assert(quoteRet.rel == std::strong_ordering::less);
        uint64_t toPoolAmount {
            bisect(quoteRet.get_unexceeded_ratio(), in.quote.value(),
                [&](uint64_t toPool) { return nondecreasing_releation_quote(toPool); })
        };
        return make_toPool(true, toPoolAmount);
    } else { // need to push base to pool
        assert(quoteRet.rel != std::strong_ordering::less);
        auto toPoolAmount {
            bisect(baseRet.get_unexceeded_ratio(), in.base.value(),
                [&](uint64_t toPool) { return nondecreasing_relation_base(toPool); })
        };
        return make_toPool(false, toPoolAmount);
    }
}
}

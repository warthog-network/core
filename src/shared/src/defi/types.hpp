#pragma once
#include "general/base_elements.hpp"
#include "general/funds.hpp"
#include "uint64/types.hpp"
namespace defi {
struct BaseQuote : public CombineElements<BaseEl, QuoteEl> {
    using CombineElements::CombineElements;
    BaseQuote(const defi::BaseQuote_uint64& b)
        : CombineElements(b.base, Wart::from_funds(b.quote))
    {
    }
};

// struct Pool {
//     Wart wart;
//     Funds_uint64 base;
//     Funds_uint64 shares;
// };

}

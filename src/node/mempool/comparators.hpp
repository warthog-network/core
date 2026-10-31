#pragma once
#include "block/chain/height.hpp"
#include "defi/token/account_token.hpp"
#include "general/lex_compare.hpp"
#include "txset.hpp"
#include <type_traits>

namespace mempool {

struct ComparatorTxHeight {
    using const_iter_t = Txset::const_iter_t;
    using is_transparent = std::true_type;
    inline bool operator()(const_iter_t i1, Height h2) const
    {
        return i1->txHeight < h2;
    }
    inline bool operator()(const_iter_t it1, const_iter_t it2) const
    {
        return lex_less_by<TxHeight, TransactionId>(it1, it2);
    }
};

struct ComparatorPin {
    using const_iter_t = Txset::const_iter_t;
    using is_transparent = std::true_type;
    inline bool operator()(const_iter_t i1, Height h2) const
    {
        return (i1->pin_height() < h2);
    }
    inline bool operator()(const_iter_t it1, const_iter_t it2) const
    {
        return lex_less_by<PinHeight, TransactionId>(it1, it2);
    }
};

struct ComparatorHash {
    using const_iter_t = Txset::const_iter_t;
    using is_transparent = std::true_type;
    inline bool operator()(const_iter_t i1, const_iter_t i2) const
    {
        if (i1->txhash == i2->txhash) // tie breaker for identical hash
            return lex_less_by<TransactionId>(i1, i2);

        return i1->txhash < i2->txhash;
    }
    inline bool operator()(const_iter_t i1, const HashView rhs) const
    {
        return i1->txhash < rhs;
    }
    inline bool operator()(HashView lhs, const_iter_t i2) const
    {
        return lhs < i2->txhash;
    }
};
struct ComparatorAccountFee {
    using const_iter_t = Txset::const_iter_t;
    using is_transparent = std::true_type;
    bool operator()(const_iter_t it1, const_iter_t it2) const
    {
        using namespace extractors;
        return lex_less_by<AccountId, CompactUInt, NonceId>(it1, it2);
    }
    inline bool operator()(const_iter_t i1, const AccountId& rhs) const
    {
        return lex_less_by<AccountId>(i1, rhs);
    }
    inline bool operator()(const AccountId& lhs, const_iter_t i2) const
    {
        return lex_less_by<AccountId>(lhs, i2);
    }
};
struct ComparatorTokenAccountFee {
    using const_iter_t = Txset::const_iter_t;
    using is_transparent = std::true_type;
    bool operator()(const_iter_t it1, const_iter_t it2) const
    {
        return lex_less_by<TokenId, AccountId, CompactUInt, NonceId>(it1, it2);
    }
    inline bool operator()(const_iter_t i1, const AccountToken& rhs) const
    {
        return lex_less_by<TokenId, AccountId>(i1, rhs);
    }
    inline bool operator()(const AccountToken& lhs, const_iter_t i2) const
    {
        return lex_less_by<TokenId, AccountId>(lhs, i2);
    }
};
}

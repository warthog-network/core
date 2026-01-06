#pragma once

#include "block/body/account_id.hpp"
#include "crypto/address.hpp"
#include "crypto/hash.hpp"
#include "defi/token/id.hpp"
#include "tools/alternative.hpp"

namespace api {
struct AssetIdOrHash : public wrt::alternative<AssetId, AssetHash> {
    using alternative::alternative;
};
struct TokenSpec {
    AssetHash assetHash;
    bool isLiquidity;
    constexpr TokenSpec(AssetHash ah, bool liquidity)
        : assetHash(std::move(ah))
        , isLiquidity(liquidity)
    {
    }
    static const TokenSpec WART;

    static TokenSpec parse_throw(std::string_view s)
    {
        if (auto o { parse(s) })
            return *o;
        throw Error(EINV_TOKEN);
    }

    std::string to_string() const
    {
        return (isLiquidity ? "liquidity" : "asset") + std::string(":") + assetHash.hex_string();
    }

    static wrt::optional<TokenSpec> parse(std::string_view s)
    {
        auto pos { s.find(":") };
        if (pos == s.npos)
            return {};
        auto indicatorStr { s.substr(0, pos) };
        bool liquidity;
        if (indicatorStr == "liquidity") {
            liquidity = true;
        } else if (indicatorStr == "asset") {
            liquidity = false;
        } else {
            return {};
        }
        auto hashStr { s.substr(pos + 1) };
        auto ah { AssetHash::try_parse(hashStr) };
        if (!ah)
            return {};
        return TokenSpec { *ah, liquidity };
    }
};

constexpr TokenSpec TokenSpec::WART = { AssetHash::WART, false };

struct TokenIdOrSpec : public wrt::alternative<TokenId, TokenSpec> {
    using alternative::alternative;
    auto to_string() const
    {
        return visit([](const auto& a) { return a.to_string(); });
    }
};
struct AccountIdOrAddress : public wrt::alternative<AccountId, Address> {
    using alternative::alternative;
};
}

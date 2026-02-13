#pragma once
#include "api/types/input.hpp"
#include "crypto/hash.hpp"
#include "general/funds.hpp"
#include "general/hex.hpp"
#include "tabs_fwd.hpp"

#include <string>

struct TokenInfo {
    std::string assetName;
    api::TokenSpec spec;
    TokenPrecision assetPrec;
    std::string market() const { return assetName + "/WART"; }
    std::string liquidity_name() const { return assetName + "-LIQUIDITY"; }
    std::string to_string() const { return spec.to_string() + " (" + assetName + ")"; }
    TokenPrecision prec() const { return spec.isLiquidity ? TokenPrecision::LIQUIDITY : assetPrec; };
    std::string pretty_name() const { return spec.isLiquidity ? liquidity_name() : assetName; }
    constexpr TokenInfo(std::string name, api::TokenSpec spec, TokenPrecision precision)
        : assetName(std::move(name))
        , spec(std::move(spec))
        , assetPrec(precision)
    {
    }
    static const TokenInfo DEMO;
    static const TokenInfo WART;
};
inline const TokenInfo TokenInfo::DEMO { "DEMO", api::TokenSpec::WART, 8 };
inline const TokenInfo TokenInfo::WART { "WART", api::TokenSpec::WART, TokenPrecision::WART };

struct AssetInfo {
    std::string name;
    AssetHash hash;
    TokenPrecision precision;
    TokenInfo token(bool isLiquidity)
    {
        return { name, api::TokenSpec(hash, isLiquidity), precision };
    }
    std::string market() const { return name + "/WART"; }
    std::string liquidity_name() const { return name + "-LIQUIDITY"; }
    std::string to_string() const { return name + " (" + serialize_hex(hash) + ")"; }
    AssetInfo(std::string name, AssetHash hash, TokenPrecision precision)
        : name(std::move(name))
        , hash(std::move(hash))
        , precision(precision)
    {
    }
    static AssetInfo demo()
    {
        return { "DEMO", AssetHash::zero(), 8 };
    }
};

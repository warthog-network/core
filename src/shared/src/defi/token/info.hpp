#pragma once
#include "block/body/account_id.hpp"
#include "block/chain/height.hpp"
#include "defi/token/asset.hpp"
struct AssetDetailData {
    NonzeroHeight height;
    AccountId ownerAccountId;
    Funds_uint64 totalSupply;
    TokenId group_id;
    std::optional<TokenId> parent_id;
};
struct AssetPriceData {
    double latestPrice;
};
struct AssetDetail: public AssetBasic, public AssetDetailData, public AssetPriceData {
};

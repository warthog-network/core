#pragma once
#include "block/body/labels.hpp"
#include "block/version.hpp"
#include "general/errors.hpp"
#include "general/static_string.hpp"
#include <cstdint>

template <uint32_t minversion, Error error>
struct MinBlockVersion {
    static bool allows_blockversion(BlockVersion v)
    {
        return v.value() >= minversion;
    }
    static void validate_blockversion_throw(BlockVersion v)
    {
        if (!allows_blockversion(v))
            throw error;
    }
};
using MinBlockV4 = MinBlockVersion<4, Error(EBLOCKV4)>;

struct AcceptAllBlockVersion {
    static bool allows_blockversion(BlockVersion) { return true; }
    static void validate_blockversion_throw(BlockVersion) { }
};

template <StaticString LABEL, typename VersionRule>
struct MakeTransaction : public VersionRule {
    static constexpr StaticString label { LABEL };
};

// template <typename T>
// bool validate_blockversion_throw(BlockVersion v) { return T::validate_blockversion_throw(v); }
// template <typename T>
// bool validate_blockversion_throw(const T&, BlockVersion v) { return T::validate_blockversion_throw(v); }

// Wart transfers are supported from beginning
using IsWartTransfer = MakeTransaction<block::labels::wartTransfer, AcceptAllBlockVersion>;

// These transaction types are only supported from block version v4
using IsTokenTransfer = MakeTransaction<block::labels::tokenTransfer, MinBlockV4>;
using IsAssetCreate = MakeTransaction<block::labels::assetCreation, MinBlockV4>;
using IsLimitSwap = MakeTransaction<block::labels::limitSwap, MinBlockV4>;
using IsLiquidityDeposit = MakeTransaction<block::labels::liquidityDeposit, MinBlockV4>;
using IsLiquidityWithdrawal = MakeTransaction<block::labels::liquidityWithdrawal, MinBlockV4>;
using IsCancelation = MakeTransaction<block::labels::cancelation, MinBlockV4>;

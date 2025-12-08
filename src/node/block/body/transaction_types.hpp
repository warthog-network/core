#pragma once
#include "block/version.hpp"
#include "general/errors.hpp"
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

template <typename T>
bool validate_blockversion_throw(BlockVersion v) { return T::validate_blockversion_throw(v); }
template <typename T>
bool validate_blockversion_throw(const T&, BlockVersion v) { return T::validate_blockversion_throw(v); }

// Wart transfers are supported from beginning
using IsWartTransfer = AcceptAllBlockVersion;

// These transaction types are only supported from block version v4
using IsTokenTransfer = MinBlockV4;
using IsAssetCreate = MinBlockV4;
using IsLimitSwap = MinBlockV4;
using IsLiquiditySwap = MinBlockV4;
using IsLiquidityDeposit = MinBlockV4;
using IsLiquidityWithdrawal = MinBlockV4;
using IsCancelation = MinBlockV4;

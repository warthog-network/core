#pragma once
#include "block/chain/history/index.hpp"
#include "crypto/hash.hpp"
#include <variant>

namespace api {
struct HistoryIdOrHash {
    std::variant<HistoryId, TxHash> data;
};
}

#pragma once
#include "block/chain/history/index.hpp"
#include "crypto/hash.hpp"
#include <variant>

namespace API {
struct HistoryIdOrHash {
    std::variant<HistoryId, Hash> data;
};
}

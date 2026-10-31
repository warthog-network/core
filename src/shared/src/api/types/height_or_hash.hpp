#pragma once
#include "block/chain/height.hpp"
#include "crypto/hash.hpp"
#include <variant>

namespace api {
struct HeightOrHash {
    std::variant<Height, Hash> data;
};
}

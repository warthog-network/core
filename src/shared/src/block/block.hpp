#pragma once
#include "block/body/body.hpp"
#include "block/header/header.hpp"
#include "block_fwd.hpp"

namespace block {
struct BlockData {
    NonzeroHeight height;
    Header header;
    VersionedBodyData body;
    BlockData(NonzeroHeight height, Header header, BodyData body);
    [[nodiscard]] Block parse_throw(ParseAnnotations* anotations = nullptr) &&;
};

struct Block {
    NonzeroHeight height;
    Header header;
    Body body;
    auto tx_ids(PinHeight minPinHeight) const { return body.tx_ids(height, minPinHeight); }
    Block(NonzeroHeight height, HeaderView header, Body body);
};
}

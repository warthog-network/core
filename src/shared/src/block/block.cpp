#include "block.hpp"
#include "block/header/header_impl.hpp"
namespace block {

BlockData::BlockData(NonzeroHeight height, Header header, BodyData body)
    : height(std::move(height))
    , header(std::move(header))
    , body(std::move(body), this->header.version())
{
}
Block BlockData::parse_throw(ParseAnnotations* annotations) &&
{
    return { height, std::move(header), Body::parse_throw(std::move(body), height, annotations) };
}

Block::Block(NonzeroHeight nh, HeaderView header, Body b)
    : height(std::move(nh))
    , header(std::move(header))
    , body(std::move(b))
{
    if (header.merkleroot() != body.merkle_root(height))
        throw Error(EMROOT);
}
}

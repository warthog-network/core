#include "hex.hpp"
#include "general/byte_order.hpp"

std::string serialize_hex(uint32_t v){
    uint32_t network = hton32(v);
    return serialize_hex({ (const uint8_t*)&network, 4 });
}
std::string serialize_hex(std::span<const uint8_t> in)
{
    std::string out;
    out.resize(2 * in.size());
    serialize_hex(in,out.begin());
    return out;
}

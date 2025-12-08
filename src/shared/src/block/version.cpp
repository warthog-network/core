#include "version.hpp"
#include "block/chain/height.hpp"
#include "general/is_testnet.hpp"
#include "general/params.hpp"

BlockVersion BlockVersion::hardcoded_for_params(NonzeroHeight h)
{
    if (is_testnet()) {
        if (h.value() <= 2)
            return BlockVersion::v3;
        return BlockVersion::v4;
    } else {
        if (h.value() <= TOKENSTARTHEIGHT)
            return BlockVersion::v3;
        return BlockVersion::v4;
    }
}

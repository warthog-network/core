#pragma once
#include <cstdint>
constexpr bool DEBUG_PARAMS = false;

constexpr uint64_t COINUNIT = 100000000; // 1 UNIT of the coin in the smallest representable quantity (i.e. implicit definition of decimal number)

/////////////
// Communication parameters
/////////////
constexpr uint16_t DEFAULT_ENDPOINT_PORT = 9186; // this value is also in cmdline/cmdoptions.ggo
constexpr uint32_t MAXBUFFER = 3 * 1000 * 1000; // 3 MB send buffer size bound (connection is closed if exceeded)

/////////////
// Genesis Block
/////////////
constexpr const char GENESISSEED[] = "The New York Times International Edition 29/06/2023: 'Water troubles: A preview'"; // use SHA256(GENESISSEED) to seed genensis block prev hash
constexpr const char GENESISSEED_TESTNET[] = "DeFi Testnet restart 24/10/26"; // use SHA256(GENESISSEED) to seed genensis block prev hash
constexpr uint8_t GENESISDIFFICULTYEXPONENT = (DEBUG_PARAMS ? 18 : 32); // 2^(<this number>) is the expected number of tries to mine the first block
constexpr uint64_t GENESISBLOCKREWARD = 3 * COINUNIT; // total reward mined in every block before halving occurs

/////////////
// Block parameters
/////////////
constexpr uint64_t BLOCKTIME = 20; // difficulty is rebalanced such that one block takes BLOCKTIME seconds
constexpr uint32_t MAXBLOCKSIZE = 35000; // max size per block (i.e. approximateily 1 MB every 10 minutes)

/////////////
// Batch parameters
/////////////
constexpr uint32_t HEADERBATCHSIZE = DEBUG_PARAMS ? 16 : 8640; // number of headers that are returned in one batch, reqestable from offsets equal to multiples of this number
constexpr uint32_t MAXBLOCKBATCHSIZE = 30; // maximal number of blocks that can be requested
constexpr uint32_t BLOCKBATCHSIZE = 30; // number of blocks that the node requests in one batch

/////////////
// Timestamp parameters
/////////////
constexpr uint32_t TOLERANCEMINUTES = 10; // tolerate block timestamps this much larger than current time
constexpr uint32_t MEDIAN_N = DEBUG_PARAMS ? 5 : 50; // the median of this many block timestamps must not decrease

/////////////
// Halving
/////////////
constexpr uint32_t HALVINTINTERVAL = DEBUG_PARAMS ? 400 : (2 * 365 * 24 * 60 * 60) / BLOCKTIME; // halving approx. every two years
constexpr uint64_t WARTMAXSUPPLY = GENESISBLOCKREWARD * HALVINTINTERVAL * 2;
constexpr uint64_t TOKENLIMITSUPPLY = 0x8FFFFFFFFFFFFFFF;

/////////////
// Difficulty adjustment heights
/////////////
inline constexpr uint32_t retarget_floor(uint32_t height)
{
    constexpr uint32_t fourhours = 4 * 60 * 60 / BLOCKTIME;
    constexpr uint32_t tenmonths = 10 * 30 * 24 * 60 * 60 / BLOCKTIME;
    static_assert(tenmonths % HEADERBATCHSIZE == 0);
    static_assert(60 % BLOCKTIME == 0);
    if (height < tenmonths) {
        const uint32_t val = (height / fourhours) * fourhours;
        if (val == 0)
            return 1;
        return val;
    } else {
        return (height / HEADERBATCHSIZE) * HEADERBATCHSIZE;
    }
}

/////////////
// Janushash Start
/////////////
constexpr uint32_t JANUSV1RETARGETSTART = retarget_floor(745200);
constexpr uint32_t JANUSV2RETARGETSTART = retarget_floor(769680);
constexpr uint32_t JANUSV3RETARGETSTART = retarget_floor(776880);
constexpr uint32_t JANUSV4RETARGETSTART = retarget_floor(809280);
constexpr uint32_t JANUSV5RETARGETSTART = 855000;
constexpr uint32_t JANUSV6RETARGETSTART = 879500;
constexpr uint32_t JANUSV7RETARGETSTART = 987000;
constexpr uint32_t JANUSV8BLOCKV3START = retarget_floor(1658880);
constexpr uint32_t NEWMERKLEROOT = 900000;
constexpr uint32_t NEWBLOCKSTRUCUTREHEIGHT = 981500;
constexpr uint32_t TOKENSTARTHEIGHT = 9815000;

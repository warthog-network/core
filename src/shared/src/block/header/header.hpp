#pragma once
#include "block/header/view.hpp"
#include "block/version.hpp"
#include <array>
#include <cstdint>

class Height;
class POWVersion;
constexpr size_t HEADERBYTELENGTH = 80;
class Header : public std::array<uint8_t, HEADERBYTELENGTH> {
public:
    using array::array;
    using array::operator=;
    using array::data;
    using array::size;
    Header() { };
    Header(std::string_view);
    Header(std::array<uint8_t, 80> arr)
        : array(std::move(arr)) { };
    Header(std::span<const uint8_t, 80>& s);
    Header(HeaderView hv)
    {
        memcpy(data(), hv.data(), 80);
    }
    Header& operator=(HeaderView hv) { return *this = Header(hv); }
    operator HeaderView() const { return HeaderView(data()); }
    inline Target target(NonzeroHeight, bool testnet) const;
    inline bool validPOW(const Hash&, POWVersion) const;
    inline HashView prevhash() const;
    inline HashView merkleroot() const;
    inline void set_merkleroot(std::array<uint8_t, 32>);
    inline uint32_t timestamp() const;
    void set_timestamp(std::array<uint8_t, 4>);
    BlockVersion version() const;
    inline TargetV1 target_v1() const;
    inline TargetV2 target_v2() const;
    inline double janus_number() const;
    inline uint32_t nonce() const;
    void set_nonce(std::array<uint8_t, 4>);
    inline BlockHash hash() const;
    constexpr static size_t byte_size()
    {
        return HEADERBYTELENGTH;
    }
};

struct HeightHeader {
    NonzeroHeight height;
    Header header;
    bool operator==(const HeightHeader&) const = default;
};

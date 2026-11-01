#pragma once
#include "block/chain/height.hpp"
#include "block/chain/range.hpp"
#include "block/header/header.hpp"
#include "general/descriptor.hpp"
#include "general/errors_forward.hpp"
#include "general/fwd.hpp"
#include <cstddef>
#include <cstdint>

#include <string>
#include <vector>

class Writer;
class Reader;

struct WithNonce : public IsUint32 {
    WithNonce(uint32_t n)
        : IsUint32(n)
    {
    }
    using IsUint32::IsUint32;
    auto&& nonce(this auto&& self) { return FWD(self).val; }
};
struct RandNonce : public WithNonce {
    RandNonce();
    RandNonce(uint32_t nonce)
        : WithNonce { nonce } { };
};

struct String16 {
    String16();
    explicit String16(std::string data);
    String16(Reader& r);

    size_t byte_size() const;
    friend Writer& operator<<(Writer&, const String16&);

    std::string data;
};

class RTCPeers {
    struct Element {
        uint32_t key;
        String16 sdp; // webrtc sdp
    };

public:
    RTCPeers();
    RTCPeers(Reader& r);
    std::vector<Element> entries;
    size_t byte_size() const;
    friend Writer& operator<<(Writer&, const RTCPeers&);
};

namespace msg {

template <typename T>
struct VectorRest : public std::vector<T> {
    using std::vector<T>::vector;
    VectorRest(std::vector<T> v)
        : std::vector<T>(std::move(v))
    {
    }
    VectorRest(Reader&);
    size_t byte_size() const;
};


template <typename T>
class Optional : public std::optional<T> {
public:
    using std::optional<T>::optional;
    size_t byte_size() const;
    Optional(Reader&);
    Optional(std::optional<T> o)
        : std::optional<T>(std::move(o))
    {
    }
};
template <typename T>
class ReadRest : public T {
public:
    ReadRest(T t)
        : T(std::move(t))
    {
    }
    using T::T;
    ReadRest(Reader& r);
};

}

struct CurrentAndRequested {
    CurrentAndRequested() { }
    CurrentAndRequested(Reader& r);
    friend Writer& operator<<(Writer&, const CurrentAndRequested&);
    size_t byte_size() const;
    std::optional<Header> requested;
    std::optional<Header> current;
};

struct HeaderBatchSelector {
    Descriptor descriptor;
    NonzeroHeight startHeight;
    NonzeroHeight end() const { return startHeight + length; }
    uint16_t length;
    HeaderRange header_range() const { return HeaderRange(startHeight, startHeight + length); };
    HeaderBatchSelector(Descriptor d, NonzeroHeight s, uint16_t l)
        : descriptor(d)
        , startHeight(s)
        , length(l) { };
    HeaderBatchSelector(Reader& r);
    friend Writer& operator<<(Writer&, const HeaderBatchSelector&);
    static constexpr size_t byte_size() { return Descriptor::byte_size() + NonzeroHeight::byte_size() + sizeof(length); }
};

template <Error parseHeightZeroError>
class NonzeroHeightParser : public NonzeroHeight {
public:
    using NonzeroHeight::NonzeroHeight;
    NonzeroHeightParser(NonzeroHeight& h)
        : NonzeroHeight(h)
    {
    }
    NonzeroHeightParser(Reader& r)
        : NonzeroHeight(Height(r).nonzero_throw(parseHeightZeroError))
    {
    }
};

#pragma once

#include "general/byte_order.hpp"
#include "general/fwd.hpp"
#include "serializer_fwd.hxx"
#include "view_fwd.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

struct ByteCounter {
    size_t N { 0 };
    constexpr void add_size(size_t size)
    {
        N += size;
    }
    constexpr void skip(size_t size) { add_size(size); }
};

struct MerkleByteCounter {
    struct Dummy { };
    ByteCounter writer;
    Dummy hook() { return {}; }
};

template <typename S, typename T>
concept MerkleSerializing = MerkleSerializer<S> && requires(S& s, const T& t) {
    { t.serialize(s) };
};

constexpr auto&& operator<<(RawSerializer auto&& s, std::span<const uint8_t> sp)
{
    s.write(sp);
    return FWD(s);
}
constexpr auto& operator<<(ByteCounter& s, std::span<const uint8_t> sp)
{
    s.add_size(sp.size());
    return s;
}

constexpr auto&& operator<<(RawSerializer auto&& s, ByteSwappable auto v)
{
    auto valBe { network_byte_swap(v) };
    std::span<const uint8_t> sp((uint8_t*)&valBe, sizeof(v));
    return FWD(s) << sp;
}
constexpr auto& operator<<(ByteCounter& s, ByteSwappable auto v)
{
    s.add_size(sizeof(v));
    return s;
}

template <typename T>
auto&& operator<<(RawSerializer auto&& s, const std::optional<T>& o)
{
    if (o)
        return FWD(s) << uint8_t(1) << *o;
    else
        return FWD(s) << uint8_t(0);
}
template <typename T>
auto&& operator<<(ByteCounter& s, const std::optional<T>& o)
{
    s.add_size(1);
    if (o)
        return s << *o;
    return s;
}

template <size_t N>
constexpr auto& operator<<(ByteCounter& s, const View<N>&)
{
    s.add_size(N);
    return s;
}

constexpr auto&& operator<<(RawSerializer auto&& s, bool b)
{
    return FWD(s) << (b ? uint8_t(1) : uint8_t(0));
}

constexpr auto&& operator<<(RawSerializer auto&& s, std::string_view r)
{
    std::span sp(reinterpret_cast<const uint8_t*>(r.data()), r.size());
    return FWD(s) << sp;
}

template <RawSerializer S, typename T>
requires RawSerializing<S, T>
constexpr auto&& operator<<(S&& s, const T& t)
{
    t.serialize(s);
    return std::forward<S>(s);
}

template <MerkleSerializer M, typename T>
requires MerkleSerializing<M, T>
constexpr auto&& operator<<(M&& s, const T& t)
{
    t.serialize(s);
    return std::forward<M>(s);
}

template <typename T>
requires RawSerializing<ByteCounter, T>
constexpr size_t count_bytes(const T& t)
{
    return (ByteCounter() << t).N;
}

template <typename T>
requires MerkleSerializing<MerkleByteCounter, T>
constexpr size_t count_bytes(const T& t)
{
    return (MerkleByteCounter() << t).writer.N;
}

#pragma once

#include "general/serializer.hxx"
#include "wrt/optional.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

class Writer {
    template <size_t... Is, typename... Ts>
    Writer& write_tuple(const std::tuple<Ts...>& t)
    {
        return (*this) << std::get<Is...>(t);
    }

public:
    Writer(uint8_t* pos, size_t n)
        : pos(pos)
        , end(pos + n)
    {
    }
    Writer(std::span<uint8_t> s)
        : Writer(s.data(), s.size())
    {
    }
    ~Writer() { assert(pos <= end); }

    void write(const std::span<const uint8_t>& s)
    {
        assert(remaining() >= s.size());
        memcpy(pos, s.data(), s.size());
        pos += s.size();
    }

    template <typename... Ts>
    Writer& operator<<(const std::tuple<Ts...>& t)
    {
        return write_tuple<std::index_sequence_for<Ts...>, Ts...>(t);
    }

    uint8_t* cursor() { return pos; }
    void skip(size_t bytes) { pos += bytes; }
    size_t remaining()
    {
        assert(end >= pos);
        return end - pos;
    }

private:
    uint8_t* pos;
    uint8_t* const end;
};

template <typename T>
std::vector<uint8_t> to_bytes(T&& t)
{
    std::vector<uint8_t> out;
    out.resize(count_bytes(std::forward<T>(t)));
    Writer w(out);
    w << t;
    return out;
}

#pragma once

#include "block/chain/worksum.hpp"
#include "general/byte_order.hpp"
#include "general/errors.hpp"
#include "view.hpp"
#include "wrt/optional.hpp"
#include <span>

// inline funcitons for access
inline uint64_t readuint64(const uint8_t* pos)
{
    uint64_t val;
    memcpy(&val, pos, 8);
    return ntoh64(val);
}
inline uint32_t readuint32(const uint8_t* pos)
{
    uint32_t res;
    memcpy(&res, pos, 4);
    return ntoh32(res);
}
inline uint16_t readuint16(const uint8_t* pos)
{
    uint16_t res;
    memcpy(&res, pos, 2);
    return ntoh16(res);
}
inline uint8_t readuint8(const uint8_t* pos) { return *(pos); }

template <size_t N>
class ReaderCheck;

// byte sequence stream-like reader with self-advancing cursor
class Reader {
    inline void read(void* out, size_t bytes)
    {
        auto newpos { pos + bytes };
        if (newpos > end)
            throw Error(EMSGINTEGRITY);
        memcpy(out, pos, bytes);
        pos = newpos;
    }
    template <typename T>
    T read()
    {
        T t;
        read(&t, sizeof(T));
        return t;
    }

public:
    using is_reader = std::true_type;
    Reader(std::span<const uint8_t> s)
        : begin(s.data())
        , pos(begin)
        , end(s.data() + s.size())
    {
    }
    template <size_t N>
    operator std::array<uint8_t, N>()
    {
        return view<N>();
    }
    uint64_t uint64()
    {
        static_assert(sizeof(uint64_t) == 8);
        return ntoh64(read<uint64_t>());
    }
    uint32_t uint32()
    {
        static_assert(sizeof(uint32_t) == 4);
        return ntoh32(read<uint32_t>());
    }
    uint16_t uint16()
    {
        static_assert(sizeof(uint16_t) == 2);
        return ntoh16(read<uint16_t>());
    }
    uint8_t uint8()
    {
        return read<uint8_t>();
    }
    operator bool()
    {
        return uint8() != 0;
    }
    operator uint64_t()
    {
        return uint64();
    }
    operator uint32_t()
    {
        return uint32();
    }
    operator uint16_t()
    {
        return uint16();
    }
    operator uint8_t()
    {
        return uint8();
    }
    template <typename... T>
    operator std::tuple<T...>()
    {
        return { static_cast<T>(*this)... };
    }
    template <size_t N>
    View<N> view()
    {
        View<N> v(pos);
        skip(N);
        return v;
    }

    template <size_t N>
    operator View<N>()
    {
        return view<N>();
    }

    struct Optional {
        Reader& r;
        template <typename T>
        operator wrt::optional<T>()
        {
            if (r.uint8())
                return T { r };
            return {};
        }
    };
    Optional optional() { return { *this }; }
    template <typename T, size_t N = T::size()>
    requires std::derived_from<T, View<N>>
    [[nodiscard]] T view()
    {
        T t(pos);
        skip(N);
        return { t };
    }
    void copy_checkrange(void* dst, size_t len)
    {
        read(dst, len);
    }
    Worksum worksum()
    {
        Worksum::fragments_type arr;
        for (size_t i = 0; i < arr.size(); ++i) {
            arr[i] = uint32();
        }
        return Worksum(arr);
    }
    operator Worksum()
    {
        return worksum();
    }
    std::span<const uint8_t> take_span(size_t len)
    {
        auto p = pos;
        skip(len);
        return { p, len };
    }
    std::string_view take_string_view(size_t len)
    {
        auto p = pos;
        skip(len);
        return { (const char*)p, len };
    }
    std::span<const uint8_t> span_4()
    {
        return take_span(uint32());
    }
    std::span<const uint8_t> rest()
    {
        return take_span(remaining());
    };

    void seek(const uint8_t* cursor) { pos = cursor; }
    void skip(size_t nbytes)
    {
        pos += nbytes;
        if (pos > end) {
            throw Error(EMSGINTEGRITY);
        }
    };
    bool eof() const { return pos == end; }
    void consumed() { pos = end; }
    const uint8_t* cursor() const { return pos; }
    size_t offset() const { return pos - begin; }
    size_t remaining() const { return end - pos; }

private:
    const uint8_t* begin;
    const uint8_t* pos;
    const uint8_t* end;
};

template <typename T>
struct ReadExhaustive {
    T t;
    ReadExhaustive(T t)
        : t(std::move(t))
    {
    }
    template <typename out_t>
    operator out_t() &&
    {
        Reader r(t);
        out_t o(r);
        if (!r.eof())
            throw Error(EEXCESSBYTES);
        return o;
    }
};

template<typename T>
T from_bytes(std::span<const uint8_t> s){
    Reader r(s);
    return {r};
}

template <size_t N>
class ReaderCheck {
public:
    ReaderCheck(Reader& r)
        : r(r)
        , beginPos(r.cursor())
    {
    }
    operator Reader&()
    {
        return r;
    }
    template <size_t M>
    operator ReaderCheck<M>()
    {
        return { r };
    }
    void assert_read_bytes()
    {
        assert(r.cursor() == beginPos + N);
    }
    Reader& r;
    const uint8_t* const beginPos;
};

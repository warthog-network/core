#pragma once
#include "general/reader_declaration.hpp"
#include "general/serializer_fwd.hxx"
#include "nlohmann/json_fwd.hpp"
#include <cstdint>

class Writer;

template <typename T>
requires std::is_integral_v<T>
struct IsValue {
    constexpr T value() const { return val; }

    explicit constexpr IsValue(T t)
        : val(std::move(t))
    {
    }

    std::string to_string() const
    {
        return std::to_string(val);
    }

    bool operator==(const IsValue&) const = default;
    auto operator<=>(const IsValue&) const = default;
    T val;
};
struct IsUint32 : public IsValue<uint32_t> {
public:
    static constexpr size_t byte_size() { return sizeof(val); }
    constexpr explicit IsUint32(uint32_t val)
        : IsValue(val) { };
    IsUint32(Reader& r);

    bool operator==(const IsUint32&) const = default;
    auto operator<=>(const IsUint32&) const = default;
    operator nlohmann::json() const;

    constexpr uint32_t value() const
    {
        return val;
    }
    void serialize(RawSerializer auto& s) const
    {
        s << value();
    }
};

template <typename T>
class UInt32WithIncrement : public IsUint32 {
public:
    using IsUint32::IsUint32;
    T operator++(int)
    {
        return T(val++);
    }
};

template <typename T>
class UInt32WithOperators : public UInt32WithIncrement<T> {
public:
    using UInt32WithIncrement<T>::UInt32WithIncrement;
    using parent_t = UInt32WithOperators<T>;
    size_t operator-(T a)
    {
        return this->val - a.val;
    }
    T operator-(size_t i) const
    {
        return T(this->val - i);
    }
    T operator+(size_t i) const
    {
        return T(this->val + i);
    }
    T operator++(int)
    {
        return T(this->val++);
    }
    T operator++()
    {
        return T(++this->val);
    }
};

struct IsUint64 : public IsValue<uint64_t> {
public:
    IsUint64(Reader& r);
    static constexpr size_t byte_size() { return sizeof(val); }
    explicit constexpr IsUint64(uint64_t val)
        : IsValue(val) { };

    std::string to_string() const;
    operator nlohmann::json() const;
    constexpr uint64_t value() const
    {
        return val;
    }
    void serialize(RawSerializer auto& s) const
    {
        s << value();
    }
};

template <typename T>
class UInt64WithIncrement : public IsUint64 {
public:
    using IsUint64::IsUint64;
    T operator++(int)
    {
        return T(val++);
    }
};

template <typename T>
class UInt64WithOperators : public UInt64WithIncrement<T> {
public:
    using UInt64WithIncrement<T>::UInt64WithIncrement;
    using parent_t = UInt64WithOperators<T>;
    size_t operator-(T a)
    {
        return this->val - a.val;
    }
    T operator-(size_t i) const
    {
        return T(this->val - i);
    }
    T operator+(size_t i) const
    {
        return T(this->val + i);
    }
    T operator++(int)
    {
        return T(this->val++);
    }
    T operator++()
    {
        return T(++this->val);
    }
};

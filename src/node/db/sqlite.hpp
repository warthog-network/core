#pragma once
#include "SQLiteCpp/Column.h"
#include "SQLiteCpp/SQLiteCpp.h"
#include "spdlog/spdlog.h"
#include "sqlite_fwd.hpp"
#include "type_conv.hpp"

namespace sqlite {


template <typename T>
T Column::convert() const
{
    try {
        return static_cast<T>(ColumnConverter(*this));
    } catch (const std::exception& e) {
        spdlog::error("Database error, cannot convert value");
        throw;
    }
}

template <typename T>
Column::Opt::operator std::optional<T>() const
{
    if (c.isNull())
        return {};
    return c.convert<T>();
}

template <typename T>
inline Column::operator T() const
{
    if (isNull())
        throw std::runtime_error("Database value is NULL");
    return convert<T>();
}

inline Statement& Row::statement() const
{
    return st.get();
}
inline Column Row::operator[](int index) const
{
    value_assert();
    return statement().getColumn(index);
}

template <typename T>
inline T Row::get(int index) const
{
    return operator[](index);
}

template <>
inline uint64_t Row::get<uint64_t>(int index) const
{
    int64_t v { operator[](index) };
    assert(v >= 0);
    return v;
}

template <size_t N>
inline std::array<uint8_t, N> Row::get_array(int index) const
{
    value_assert();
    return statement().getColumn(index);
}

inline std::vector<uint8_t> Row::get_vector(int index) const
{
    value_assert();
    return statement().getColumn(index);
}

template <typename T>
inline Row::operator std::optional<T>()
{
    if (!hasValue)
        return {};
    return get<T>(0);
}

inline auto Row::process(auto lambda) const
{
    using ret_t = std::remove_cvref_t<decltype(lambda(*this))>;
    std::optional<ret_t> r;
    if (has_value())
        r = lambda(*this);
    return r;
}

inline void Row::value_assert() const
{
    if (!hasValue) {
        throw std::runtime_error(
            "Database error: trying to access empty result.");
    }
}
inline Row::Row(Statement& st)
    : st(st)
{
    hasValue = statement().executeStep();
}

inline Column Statement::getColumn(const int aIndex)
{
    return { SQLite::Statement::getColumn(aIndex) };
}

namespace {

template <typename T>
void bind_param(SQLite::Statement& stmt, int i, const T& a)
{
    stmt.bind(i, a);
}

template <typename T>
requires std::is_convertible_v<T, std::span<const uint8_t>>
void bind_param(SQLite::Statement& stmt, int i, const T& s)
{
    // We don't want the SQLite behavior
    // "If the third parameter to sqlite3_bind_blob() is a NULL pointer then the fourth parameter is ignored and the end result is the same as sqlite3_bind_null()."
    // We want zero length blob, not null, so we use ternary operator.
    stmt.bind(i, s.size() == 0 ? (void*)"" : s.data(), s.size());
}

// template<>
// void bind_param<std::string>(SQLite::Statement& stmt, int i, const std::string& s)
// {
//     stmt.bind(i, s.data(), s.size());
// }

struct Binder {
    using Stmt = SQLite::Statement;
    Binder(Stmt& stmt)
        : stmt(stmt)
    {
    }
    template <typename T>
    auto bind(int i, const std::optional<T>& a)
    {
        if (a)
            bind(i, *a);
        else
            stmt.bind(i); // binds null
    }
    auto bind(int i, const auto& a)
    {
        bind_param(stmt, i, bind_convert::convert(a));
    }
    Stmt& stmt;
};
}
template <typename T>
inline void Statement::bind(const int index, const T& t)
{
    Binder(*this).bind(index, t);
}

template <size_t i>
void Statement::recursive_bind()
{
}
template <size_t i, typename T, typename... Types>
void Statement::recursive_bind(T&& t, Types&&... types)
{
    bind(i, std::forward<T>(t));
    recursive_bind<i + 1>(std::forward<Types>(types)...);
}
template <typename... Types>
inline uint64_t Statement::run(Types&&... types)
{
    bind_multiple(std::forward<Types>(types)...);
    auto nchanged = exec();
    reset();
    assert(nchanged >= 0);
    return nchanged;
}

template <typename... Types, typename Lambda>
void Statement::for_each_while(Lambda lambda, Types&&... types)
{
    bind_multiple(std::forward<Types>(types)...);
    while (true) {
        auto r { next_row() };
        if (!r.has_value())
            break;
        if (!lambda(r))
            break;
    }
    reset();
}

template <typename... Types, typename Lambda>
void Statement::for_each(Lambda lambda, Types&&... types)
{
    bind_multiple(std::forward<Types>(types)...);
    while (true) {
        auto r { next_row() };
        if (!r.has_value())
            break;
        lambda(r);
    }
    reset();
}

// template <typename... Types>
// auto Statement::loop(Types&&... types)
// {
//     recursive_bind<1>(std::forward<Types>(types)...);
//     return RunningStatement { *this };
// }
}

#pragma once
#include "SQLiteCpp/Statement.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

namespace sqlite {

struct Column;
class Statement;
class RunningStatement;

class Row {

private: // data
    std::reference_wrapper<Statement> st;
    bool hasValue;

public:
    friend class Statement;
    friend class RunningStatement;
    Column operator[](int index) const;
    template <typename T>
    T get(int index) const;
    template <size_t N>
    std::array<uint8_t, N> get_array(int index) const;
    std::vector<uint8_t> get_vector(int index) const;
    template <typename T>
    operator std::optional<T>();
    bool has_value() const { return hasValue; }
    [[nodiscard]] auto process(auto lambda) const;

private:
    void value_assert() const;
    Row(Statement& st);
    Statement& statement() const;
};

class Statement : public SQLite::Statement {
public:
    using SQLite::Statement::Statement;
    Column getColumn(const int aIndex);
    template <typename T>
    void bind(const int index, const T&);
    template <size_t i>
    void recursive_bind();
    template <size_t i, typename T, typename... Types>
    void recursive_bind(T&& t, Types&&... types);
    template <typename... Types>
    auto& bind_multiple(Types&&... types)
    {
        recursive_bind<1>(std::forward<Types>(types)...);
        return *this;
    }
    template <typename... Types>
    uint64_t run(Types&&... types);
    Row next_row() { return *this; }

    // private:
    struct SingleResult : public Row {
        using Row::Row;
        ~SingleResult()
        {
            if (hasValue)
                assert(statement().executeStep() == false);
            statement().reset();
        }
    };

    template <typename... Types>
    [[nodiscard]] SingleResult one(Types&&... types)
    {
        recursive_bind<1>(std::forward<Types>(types)...);
        return SingleResult { *this };
    }

    template <typename... Types, typename Lambda>
    void for_each(Lambda lambda, Types&&... types);

    template <typename... Types, typename Lambda>
    void for_each_while(Lambda lambda, Types&&... types);

    template <typename... Types, typename Lambda>
    [[nodiscard]] auto all(Lambda lambda, Types&&... types)
    {
        using ret_t = decltype(lambda(next_row()));
        std::vector<ret_t> res;
        for_each([&](const auto& row) {
            res.push_back(lambda(row));
        },
            types...);
        return res;
    }

    template <typename... Types, typename Lambda>
    auto make_vector(Lambda lambda, Types&&... types);

    template <typename... Types>
    auto loop(Types&&... types);
};

}

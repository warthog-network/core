#pragma once
#include "general/errors.hpp"
#include <expected>
#include <optional>

template <typename T>
struct Result;
namespace result {

template <typename T>
struct GetResultT;

template <typename T>
struct GetResultT<std::expected<T, Error>> {
    using type = Result<T>;
};

template <typename T>
GetResultT<T>::type make(std::expected<T, Error>&& e)
{
    return std::move(e);
}

}
template <typename T>
struct Result : public std::expected<T, Error> {
    using parent = std::expected<T, Error>;
    Result(parent t)
        : parent(std::move(t))
    {
    }
    Result(std::optional<T> t)
        : Result(
              [&]() -> Result {
                  if (t) {
                      return Result(std::move(*t));
                  } else {
                      return Result(Error(ENOTFOUND));
                  }
              }())
    {
    }
    T value_or_throw() &&
    {
        if (this->has_value())
            return std::move(*this).value();
        throw this->error();
    }
    template <typename Self, typename F>
    constexpr auto and_then(this Self&& self, F&& f)
    {
        return result::make(std::forward<Self>(self)->parent::and_then([&f]() { return parent(std::forward<F>(f)); }));
    }

    template <typename Self, typename F>
    constexpr auto transform(this Self&& self, F&& f)
    {
        return result::make(std::forward<Self>(self)->parent::transform([&f]() { return parent(std::forward<F>(f)); }));
    }

    Result(T t)
        : std::expected<T, Error>(std::move(t))
    {
    }
    Result(Error e)
        : std::expected<T, Error>(std::unexpected(e))
    {
    }
};

template <>
struct Result<void> : public std::expected<void, Error> {
    Result(const std::optional<Error>& t)
        : Result(t ? Result(std::unexpected(*t)) : Result())
    {
    }
    Result(std::expected<void, Error> t)
        : std::expected<void, Error>(std::move(t))
    {
    }
    Result() // for Result<void> default constructor
        : std::expected<void, Error>()
    {
    }
    Result(Error e)
        : std::expected<void, Error>(std::unexpected(e))
    {
    }
};

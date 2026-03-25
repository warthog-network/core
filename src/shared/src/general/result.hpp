#pragma once
#include "general/errors.hpp"
#include "wrt/expected.hpp"
#include "wrt/optional.hpp"
template <typename T>
struct Result;
namespace result {

template <typename T>
struct GetResultT;

template <typename T>
struct GetResultT<wrt::expected<T, Error>> {
    using type = Result<T>;
};

template <typename T>
GetResultT<T>::type make(wrt::expected<T, Error>&& e)
{
    return std::move(e);
}

}
template <typename T>
struct Result : public wrt::expected<T, Error> {
    using parent = wrt::expected<T, Error>;
    Result(parent t)
        : parent(std::move(t))
    {
    }
    Result(wrt::optional<T> t)
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
        : wrt::expected<T, Error>(std::move(t))
    {
    }
    Result(Error e)
        : wrt::expected<T, Error>(tl::make_unexpected(e))
    {
    }
};

template <>
struct Result<void> : public wrt::expected<void, Error> {
    Result(const wrt::optional<Error>& t)
        : Result(t ? Result(wrt::make_unexpected(*t)) : Result())
    {
    }
    Result(wrt::expected<void, Error> t)
        : wrt::expected<void, Error>(std::move(t))
    {
    }
    Result() // for Result<void> default constructor
        : wrt::expected<void, Error>({})
    {
    }
    Result(Error e)
        : wrt::expected<void, Error>(tl::make_unexpected(e))
    {
    }
};

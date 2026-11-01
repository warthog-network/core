#pragma once
#include "general/fwd.hpp"
#include <utility>
#include <variant>
namespace wrt { // wart tools namesapce

template <typename... Rs>
struct Overload : Rs... {
    using Rs::operator()...;
};
template <typename... Rs>
Overload(Rs...) -> Overload<Rs...>;

template <typename... Ts>
struct variant : public std::variant<Ts...> {
    using std::variant<Ts...>::variant;

    template <typename... U>
    auto visit_overload(this auto&& self, U&&... u)
    {
        return FWD(self).visit(Overload { std::forward<U>(u)... });
    }

    constexpr auto visit(this auto&& self, auto&& lambda)
    {
        return std::visit(FWD(lambda), FWD(self));
    }

    template <typename T>
    [[nodiscard]] bool holds(this const auto& self)
    {
        return std::holds_alternative<T>(self);
    }

    template <typename T>
    constexpr auto&& get(this auto&& self)
    {
        return std::get<T>(FWD(self));
    }
};

}

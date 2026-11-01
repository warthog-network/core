#pragma once
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
    auto visit_overload(U&&... u) &
    {
        return visit(Overload { std::forward<U>(u)... });
    }
    template <typename... U>
    auto visit_overload(U&&... u) const&
    {
        return visit(Overload { std::forward<U>(u)... });
    }
    template <typename... U>
    auto visit_overload(U&&... u) &&
    {
        return std::move(*this).visit(Overload { std::forward<U>(u)... });
    }
    constexpr auto visit(auto&& lambda) &
    {
        return std::visit(std::forward<decltype(lambda)>(lambda), *this);
    }
    constexpr auto visit(auto&& lambda) const&
    {
        return std::visit(std::forward<decltype(lambda)>(lambda), *this);
    }
    constexpr auto visit(auto&& lambda) &&
    {
        return std::visit(std::forward<decltype(lambda)>(lambda), std::move(*this));
    }
    template <typename T>
    [[nodiscard]] bool holds() const { return std::holds_alternative<T>(*this); }
    template <typename T>
    constexpr auto& get() const { return std::get<T>(*this); }

    template <typename T>
    constexpr auto& get() { return std::get<T>(*this); }
};

}

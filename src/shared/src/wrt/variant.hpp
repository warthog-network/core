#pragma once
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
    auto visit(auto lambda) &
    {
        return std::visit(lambda, *this);
    }
    auto visit(auto lambda) const&
    {
        return std::visit(lambda, *this);
    }
    auto visit(auto lambda) &&
    {
        return std::visit(lambda, std::move(*this));
    }
    template <typename T>
    [[nodiscard]] bool holds() const { return std::holds_alternative<T>(*this); }
    template <typename T>
    auto& get() const { return std::get<T>(*this); }

    template <typename T>
    auto& get() { return std::get<T>(*this); }
};

}

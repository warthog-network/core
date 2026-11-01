#pragma once

#include "general/fwd.hpp"
#include "wrt/variant.hpp"
#include <optional>
#include <utility>


namespace wrt { // wart tools namesapce
template <typename Id, typename Alt>
struct alternative {
public:
    alternative(Id id)
        : v(std::move(id))
    {
    }
    alternative(Alt a)
        : v(std::move(a))
    {
    }

    auto visit(this auto&& self, auto&& lambda)
    {
        return FWD(self).v.visit(FWD(lambda));
    }

    template <typename... U>
    auto visit_overload(this auto&& self, U&&... u)
    {
        return FWD(self).v.visit_overload(std::forward<U>(u)...);
    }

    [[nodiscard]] std::optional<Id> map_alternative(auto&& lambda) const
    {
        return visit_overload(
            [&](Id id) -> std::optional<Id> { return id; },
            [&](const Alt& a) -> std::optional<Id> { return FWD(lambda)(a); });
    }

private:
    wrt::variant<Id, Alt> v;
};
}

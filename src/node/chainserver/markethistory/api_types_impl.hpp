#pragma once
#include "api_types.hpp"
#include "general/fwd.hpp"
#include <ranges>

namespace api {
template <typename T>
void ReversibleVector<T>::foreach (this auto&& self, auto&& lambda)
{
    if (self.reverse) {
        for (auto& e : std::views::reverse(FWD(self).elements)) {
            FWD(lambda)(e);
        }
    } else {
        for (auto& e : FWD(self).elements) {
            FWD(lambda)(e);
        }
    }
}
}

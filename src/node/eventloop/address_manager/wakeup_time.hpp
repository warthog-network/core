#pragma once

#include <chrono>
#include "wrt/optional.hpp"

namespace connection_schedule {
class WakeupTime {
public:
    using steady_clock = std::chrono::steady_clock;
    using time_point = steady_clock::time_point;
    auto pop_new_min()
    {
        auto tmp { std::move(popped_tp) };
        popped_tp.reset();
        return tmp;
    }
    [[nodiscard]] bool expired(time_point now = steady_clock::now()) const
    {
        return wakeup_tp.has_value() && wakeup_tp < now;
    }
    void reset()
    {
        *this = {};
    }

    auto& val() const { return wakeup_tp; }

    void consider(const wrt::optional<time_point>& newval)
    {
        if (newval.has_value() && (!wakeup_tp.has_value() || *wakeup_tp > *newval)) {
            wakeup_tp = newval;
            popped_tp = newval;
        }
    }

private:
    wrt::optional<time_point> wakeup_tp;
    wrt::optional<time_point> popped_tp;
};
}

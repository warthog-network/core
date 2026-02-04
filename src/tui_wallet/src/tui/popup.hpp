#pragma once
#include "gui.hpp"

namespace ui {
class PopupBase : public ftxui::ComponentBase {
protected:

    bool closed { false };
public:
    bool is_closed() const { return closed; }
};

template <typename T>
struct Popup : public PopupBase, public std::enable_shared_from_this<T> {
protected:
    auto close_callback()
    {
        return [w = this->weak_from_this()] {
            if (auto s { w.lock() })
                s->closed = true;
        };
    }

};
}

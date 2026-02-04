#include "gui.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "tui/tabs.hpp"
namespace ui {
Element GUIComponent::render_spinner(int type) const
{
    return gui.render_spinner(type);
}
void GUI::set_connected(bool set)
{
    screen.Post([this, set]() {
        _root->connected = set;
        screen.RequestAnimationFrame();
    });
}
void GUI::set_unlocked(bool set)
{
    screen.Post([this, set]() {
        _root->unlocked = set;
        screen.RequestAnimationFrame();
    });
}
void GUI::run() { screen.Loop(_root); }

GUI::GUI(CreateToken)
    : screen(ScreenInteractive::TerminalOutput())
    // , spinnerClock(weak_from_this())
    , _root(Make<RootComponent>(*this))
{
    start_spinner_thread();
}

Element GUI::render_spinner(int type)
{
    return spinner(type, spinnerStep);
}
void GUI::start_spinner_thread()
{
    using sc = std::chrono::steady_clock;
    auto work { [&]() {
        auto t { sc::now() + interval };
        std::unique_lock l(m);
        while (true) {
            if (cv.wait_until(l, t, [&]() { return this->stopRequested; }))
                break;
            if (sc::now() >= t) {
                t += interval;
                spinnerStep += 1;
                trigger_render();
            }
        }
    } };
    spinnerThread = std::thread(work);
}
void GUI::stop_spinner_thread()
{
    std::lock_guard l(m);
    stopRequested = true;
    cv.notify_all();
}

GUI::~GUI()
{
    stop_spinner_thread();
    spinnerThread.join();
}
void GUI::trigger_render()
{
    screen.PostEvent(ftxui::Event::Custom);
}
void GUI::defer(std::function<void()> f)
{
    screen.Post(std::move(f));
}

RootComponent::RootComponent(GUI& gui)
    : GUIComponent(gui)
    , mainContainer(Container::Horizontal({ Make<MainTabs>(gui) }))
{
    Add(mainContainer);
}
} // namespace ui

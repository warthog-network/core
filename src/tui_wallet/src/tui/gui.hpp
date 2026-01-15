#pragma once
#include "ftxui/component/screen_interactive.hpp"
#include "gui_fwd.hpp"
// #include "spinner_clock.hpp"
#include "ui_data.hpp"

namespace ui {
using namespace ftxui;
struct RootComponent;
struct GUIComponent {
protected:
    [[nodiscard]] static ftxui::ScreenInteractive& extract_screen(GUI&);
    [[nodiscard]] static RootComponent& extract_root(GUI&);
    GUIComponent(GUI& gui)
        : gui(gui)
    {
    }
    GUI& gui;
    auto redraw_lambda() const;
    Element render_spinner(int type = 11) const;

public:
    ScreenInteractive& gui_screen() const;
    RootComponent& gui_root() const;
};
struct NamedComponentBase : public ComponentBase {
    NamedComponentBase(std::string windowName, Components children = {})
        : ComponentBase(std::move(children))
        , windowName(windowName)
    {
    }
    std::string windowName;
};

using NamedComponent = std::shared_ptr<NamedComponentBase>;
struct NamedGUIComponent : public ui::GUIComponent, public NamedComponentBase {
    NamedGUIComponent(GUI& gui, std::string windowName, Components children = {})
        : GUIComponent(gui)
        , NamedComponentBase(std::move(windowName), std::move(children))
    {
    }
};
struct IsTab { };
template <typename T>
struct MakeTab : public IsTab, public NamedGUIComponent {
private:
public:
    using NamedGUIComponent::NamedGUIComponent;
};
template <typename T>
concept is_tab = std::derived_from<T, IsTab>;

struct GUI : public SelectedAsset, public std::enable_shared_from_this<GUI> {
    friend RootComponent;
    friend GUIComponent;

public:
    ScreenInteractive screen;

private:
    using duration = std::chrono::steady_clock::duration;
    std::thread spinnerThread;
    std::mutex m;
    std::condition_variable cv;
    bool stopRequested { false };
    std::atomic<int> spinnerStep;
    duration interval { std::chrono::milliseconds(200) };
    void start_spinner_thread();
    void stop_spinner_thread();

    std::shared_ptr<RootComponent> root;

    struct CreateToken { };

public:
    Element render_spinner(int type = 12);
    void set_connected(bool set);
    void set_unlocked(bool set);
    void trigger_render();
    static std::shared_ptr<GUI> create_instance()
    {
        return std::make_shared<GUI>(CreateToken());
    }
    GUI(CreateToken);
    ~GUI();

    void terminate() { screen.Exit(); }
    void run();
};

inline auto GUIComponent::redraw_lambda() const
{
    return [this]() { gui.trigger_render(); };
};

} // namespace ui

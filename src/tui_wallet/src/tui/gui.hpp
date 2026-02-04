#pragma once
#include "ftxui/component/screen_interactive.hpp"
#include "gui_fwd.hpp"
#include "tui/popup.hpp"
#include "ui_data.hpp"

// struct ThreadContext {
//     std::vector<std::unique_ptr<std::jthread>> threads;
//     void push_back(std::jthread t)
//     {
//         threads.push_back(std::make_unique<std::jthread>(std::move(t)));
//     }
//     void prune()
//     {
//         std::erase_if(threads, [](const std::unique_ptr<std::jthread>& t) {
//             return t->joinable();
//         });
//     }
// };
//
// template <typename T>
// struct UpdatableValue : public std::enable_shared_from_this<UpdatableValue<T>> {
// private:
//     static constexpr auto pending_tp { std::chrono::steady_clock::time_point::max() };
//
//     std::chrono::steady_clock::time_point expiration;
//     std::optional<T> value;
//     size_t callSequence { 0 };
//
//     template <typename... Args>
//     void value_update(ThreadContext& ctx, bool force, Args&&... args)
//     {
//         using sc = std::chrono::steady_clock;
//         if (!force && expiration > sc::now())
//             return;
//         expiration = pending_tp;
//         value.reset();
//         callSequence += 1;
//         ctx.push_back(T::fetch(std::forward<Args>(args)...));
//     }
//
// public:
//     void set_value(std::optional<T> newVal, size_t seq)
//     {
//         if (seq != callSequence)
//             return; // stale call
//         value = newVal;
//         const auto interval { std::chrono::seconds(20) };
//         expiration = std::chrono::steady_clock::now() + interval;
//     }
//
//     template <typename... Args>
//     const std::optional<T>& update_and_get(ThreadContext& ctx, bool force, Args&&... args)
//     {
//         value_update(ctx, force, std::forward<Args>(args)...);
//         return value;
//     }
// };
//
// template <typename T>
// struct KeyedSubscriptionsTemplate {
//     struct subscription : public std::shared_ptr<UpdatableValue<T>> {
//         subscription()
//             : std::shared_ptr<UpdatableValue<T>>(
//                   std::make_shared<UpdatableValue<T>>())
//         {
//         }
//     };
//     std::map<std::string, subscription> map;
//     template <typename... Args>
//     const auto& update_and_get(std::string key, ThreadContext ctx, bool force,
//         Args&&... args)
//     {
//         return map[key]->update_and_get(ctx, force, std::forward<Args>(args)...);
//     }
//     void set_val(std::string key, std::optional<T> o)
//     {
//         map[key]->update_value(std::move(o));
//     }
// };
//
// template <typename... Ts>
// struct KeyedSubscriptionsCombine {
//     std::tuple<KeyedSubscriptionsTemplate<Ts>...> tuple;
//     template <typename T>
//     auto& get()
//     {
//         return std::get<KeyedSubscriptionsTemplate<T>>(tuple);
//     }
// };
// using KeyedSubscriptions = KeyedSubscriptionsCombine<int, std::string>;

namespace ui {
using namespace ftxui;
struct RootComponent;
struct GUIComponent {
protected:
    [[nodiscard]] static ftxui::ScreenInteractive& extract_screen(GUI&);
    [[nodiscard]] static RootComponent& extract_root(GUI&);
    GUI& gui;

    template <typename T, typename... Ts>
    void make_popup(Ts&&... ts);
    auto redraw_lambda() const;
    Element render_spinner(int type = 12) const;

public:
    GUIComponent(GUI& gui)
        : gui(gui)
    {
    }
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

struct GUI : public std::enable_shared_from_this<GUI> {
    friend RootComponent;
    friend GUIComponent;

public:
    ScreenInteractive screen;
    wrt::optional<AssetInfo> selectedAsset;

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

    std::shared_ptr<RootComponent> _root;

    struct CreateToken { };

public:
    auto& root() const { return *_root; }
    Element render_spinner(int type = 12);
    void set_connected(bool set);
    void set_unlocked(bool set);
    void defer(std::function<void()> f);
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

struct RootComponent : public GUIComponent, public ComponentBase {
    Component mainContainer;
    bool connected { false };
    bool unlocked { false };
    std::vector<std::shared_ptr<PopupBase>> popups;

private:
    Element render_connected()
    {
        return (connected ? text("•node connected") | color(Color::Green)
                          : text("⚠ node disconnected") | color(Color::Red));
    }
    Element render_unlocked()
    {
        return (unlocked ? text("•wallet unlocked") | color(Color::Green)
                         : text("⚠ wallet locked") | color(Color::Red));
    }
    bool has_popup()
    {
        size_t i = 0;
        for (; i < popups.size(); ++i) {
            if (!popups[popups.size() - 1 - i]->is_closed()) {
                popups.erase(popups.end() - i,
                    popups.end()); // erase closed popups from list
                return true;
            }
        }
        return false;
    }

public:
    void add_popup(std::shared_ptr<PopupBase> e)
    {
        popups.push_back(std::move(e));
    };

    template <typename T, typename... Ts>
    void make_popup(Ts&& ... ts)
    {
        add_popup(Make<T>(gui, std::forward<Ts>(ts)...));
    }

    Element OnRender() override
    {
        if (has_popup())
            return popups.back()->OnRender();
        else
            return ComponentBase::OnRender();
    }
    bool OnEvent(Event event) override
    {
        if (event == Event::q) {
            gui_screen().Exit();
            return true;
        }
        if (has_popup())
            return popups.back()->OnEvent(std::move(event));
        else
            return ComponentBase::OnEvent(std::move(event));
    }
    // Tabs(
    //               Make<WalletTab>(gui), Make<WartTab>(gui), Make<AssetControlTab>(gui), Make<AssetCreateTab>(gui))
    RootComponent(GUI& gui);
};
template <typename T, typename... Ts>
inline void GUIComponent::make_popup(Ts&&... ts)
{
    gui_root().make_popup<T>(std::forward<Ts>(ts)...);
}

} // namespace ui

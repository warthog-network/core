#pragma once
#include "gui.hpp"
#include "popups.hpp"
#include "tabs.hpp"
#include "transaction.hpp"
namespace ui {
struct RootComponent : public GUIComponent, public ComponentBase {
    Component mainContainer;
    bool connected { false };
    bool unlocked { false };
    std::vector<std::shared_ptr<ui::PopupBase>> popups;

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
    void popup_notification(std::string title, std::string message);
    void popup_confirmation(KVProperties, onconfirm_generator_t);
    void add_popup(std::shared_ptr<ui::PopupBase> e)
    {
        popups.push_back(std::move(e));
    };

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
    RootComponent(GUI& gui)
        : GUIComponent(gui)
        , mainContainer(Container::Horizontal({ Make<MainTabs>(gui) }))
    {
        Add(mainContainer);
    }
};
} // namespace ui

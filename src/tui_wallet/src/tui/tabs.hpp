#pragma once
#include "api/types/shared.hpp"
#include "data/interface.hpp"
#include "ftxui/component/component.hpp" // for Dropdown, Renderer, Container
#include "ftxui/component/screen_interactive.hpp" // for ScreenInteractive
#include "ftxui/dom/elements.hpp" // for text, vbox, hbox
#include "ftxui/dom/table.hpp"
#include "general/static_string.hpp"
#include "gui.hpp"
#include "popups.hpp"
#include "transaction.hpp"
#include "validated_input.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace ftxui;
using result_cb_t = std::function<void(std::string, std::string)>;
using onconfirm_generator_t = std::function<std::function<void()>(result_cb_t)>;
namespace ui {

struct TabsBase : public ComponentBase {
    std::vector<NamedComponent> components;
    std::vector<std::string> tabNames;
    int tab_index { 0 };
    TabsBase(std::vector<NamedComponent> componentsInit)
        : components(std::move(componentsInit))
    {
        for (auto& c : components)
            tabNames.push_back({ c->windowName });

        std::vector<Component> v;
        for (auto& c : components)
            v.push_back(c);
        Add(Container::Vertical(
            { Menu(&tabNames, &tab_index, MenuOption::HorizontalAnimated()),
                Container::Tab(std::move(v), &tab_index) }));
    }
};

template <typename... Ts>
requires(is_tab<Ts> && ...)
struct Tabs : public TabsBase {
    Tabs(GUI& gui)
        : TabsBase({ NamedComponent(Make<Ts>(gui))... })
    {
    }
};

struct AutocompleteInputImpl : public ComponentBase {
    std::string input_text;
    InputOption input_option;
    Component input;

private:
    std::vector<std::string> filtered;
    int focus_index = -1;

public:
    AutocompleteInputImpl()
    {
        input_option.on_change = [&] {
            if (!input_text.empty())
                filtered = filter(input_text);
            else
                filtered.clear();
            focus_index = 0;
        };
        input = Input(&input_text, "Type...", input_option);
        Add(input);
    }
    AutocompleteInputImpl(const AutocompleteInputImpl&) = delete;

    // bool Focusable() const override { return true; }
    //
    // void SetFocus(bool focus) override { input->SetFocus(focus); }

    Element OnRender() override
    {
        auto input_element = input->Render();

        Element suggestions_element;
        if (!filtered.empty()) {
            std::vector<Element> suggestion_elems;
            for (int i = 0; i < (int)filtered.size(); ++i) {
                auto elem = text(filtered[i]);
                if (i == focus_index)
                    elem |= inverted;
                suggestion_elems.push_back(elem);
            }
            suggestions_element = vbox(std::move(suggestion_elems)) | borderLight | size(HEIGHT, LESS_THAN, 7);
        } else {
            suggestions_element = text("");
        }

        return vbox({
            input_element,
            suggestions_element,
        });
    };
    bool OnEvent(Event event) override
    {
        if (!filtered.empty()) {
            if (event == Event::ArrowDown) {
                focus_index += 1;
                if (focus_index == (int)filtered.size())
                    focus_index = 0;
                return true;
            }
            if (event == Event::ArrowUp) {
                focus_index = (focus_index - 1 + (int)filtered.size()) % (int)filtered.size();
                return true;
            }
            if ((event == Event::Return || event == Event::Tab) && focus_index != -1) {
                // Keyboard confirmation: accept suggestion
                input_text = filtered[focus_index];
                input_option.cursor_position = 0;
                filtered.clear();
                focus_index = -1;
                return true;
            }
        }
        return input->OnEvent(std::move(event));
    };

    std::vector<std::string> suggestions = {
        "apple", "ape", "aave", "applause", "apply", "banana",
        "grape", "orange", "pineapple", "strawberry", "watermelon"
    };

    std::vector<std::string> filter(std::string_view sv) const
    {
        std::vector<std::string> res;
        for (auto& s : suggestions) {
            if (s.starts_with(sv))
                res.push_back(s);
        }
        return res;
    };
};

inline Component AutocompleteInput() { return Make<AutocompleteInputImpl>(); }

inline ButtonOption ButtonRoundOption()
{
    ButtonOption option;
    option.transform = [](const EntryState& s) {
        auto element = text(s.label) | border;
        if (s.focused) {
            element |= inverted;
        }
        return element;
    };
    return option;
}

struct TransactionDetailsBase : public ComponentBase {
private:
    KVProperties properties;

public:
    TransactionDetailsBase(KVProperties p)
        : properties(std::move(p))
    {
    }
    Element OnRender() override
    {
        std::vector<std::vector<Element>> initArg;
        for (auto& [name, val] : properties.entries)
            initArg.push_back({ text(name), text(val) });
        ftxui::Table table(std::move(initArg));
        return window(text(properties.title) | center, table.Render());
    }
};

inline Component TransactionDetails(KVProperties properties)
{
    return Make<TransactionDetailsBase>(std::move(properties));
    // return Renderer([p = std::move(properties)]() -> Element {
    //   std::vector<std::vector<std::string>> initArg;
    //   for (auto &[name, value] : p.entries) {
    //     initArg.push_back({name, value});
    //   }
    //   ftxui::Table table(std::move(initArg));
    //   return window(text(p.title) | center, table.Render());
    // });
}

struct NotificationPopupBase : public ui::PopupBase {
private:
    std::string title, message;
    Element linesElement;
    Component btnOk;

public:
    NotificationPopupBase(std::string title, std::string message)
        : title(std::move(title))
        , message(std::move(message))
        , btnOk(Button("OK", [this]() { closed = true; }))
    {
        Add(btnOk);
    }
    Element OnRender() override
    {
        return window(text(title),
            vbox(paragraph(message), btnOk->Render() | center));
    }
};
struct ConfirmationComponentBase : public GUIComponent, public ui::PopupBase {
    Component txdetails;
    Component btnCancel;
    Component btnConfirm;
    std::shared_ptr<NotificationPopupBase> resultPopup;
    bool submitting { false };
    std::function<void()> onConfirm;

    ConfirmationComponentBase(GUI& gui, KVProperties txprops,
        onconfirm_generator_t);
    [[nodiscard]] auto result_cb();
    bool OnEvent(Event e) override
    {
        if (resultPopup)
            return resultPopup->OnEvent(e);
        return PopupBase::OnEvent(e);
    }
    Element OnRender() override
    {
        return vbox(text("Creating Transaction") | center, txdetails->Render(),
            (submitting
                    ? hbox(text("Submitting transaction"), render_spinner(1))
                    : hbox(btnCancel->Render(), btnConfirm->Render()))
                | center);
    }
};

// struct AssetControlTab : public MakeTab<AssetControlTab> {
//
// };

struct AssetControlTab : public MakeTab<AssetControlTab> {
    Component btnTransferAsset;
    Component btnSwap;
    Component btnTransferLiquidity;
    Component btnFarm;

private:
    void on_asset_transfer();
    void on_asset_swap();
    void on_liquidity_transfer();
    void on_liquidity_farm();
    Element OnRender() override
    {
        return window(text("Actions"),
            hbox(vbox(text("Asset") | center, separator(),
                     btnTransferAsset->Render(),
                     btnSwap->Render()),
                separator(),
                vbox(text("Liquidity") | center, separator(),
                    btnTransferLiquidity, btnFarm->Render())));
    }

public:
    AssetControlTab(GUI& gui);
};
struct AssetSelectTab : public MakeTab<AssetSelectTab> {

private:
public:
    AssetSelectTab(GUI& gui);
};
struct AssetCreateTab : public MakeTab<AssetCreateTab> {
    Component btnCreateNew;
    Component btnCreateFork;

private:
    void on_create_new();
    void on_create_fork();
    Element OnRender() override
    {
        return window(text("Actions"),
            vbox(btnCreateNew, btnCreateFork));
    }

public:
    AssetCreateTab(GUI& gui);
};

struct AssetTab : public MakeTab<AssetTab> {
    AssetTab(GUI& gui)
        : MakeTab(gui, "Assets")
    {
        Add({ Make<Tabs<AssetSelectTab, AssetCreateTab, AssetControlTab>>(gui) });
    }
};

struct WalletTab : public MakeTab<WalletTab> {
    Component address;
    Component amount;
    Component nonceId;

    Element OnRender()
    {
        return vbox({ address->Render(), amount->Render(), nonceId->Render() });
    }
    WalletTab(GUI& gui)
        : MakeTab(gui, "Wallet")
        , address(ui::LabeledValidated("Wallet:  ", validator))
        , amount(ui::LabeledValidated("Amount:  ", validator))
        , nonceId(ui::LabeledValidated("NonceId: ", validator))
    {
        Add(Container::Vertical({ address, amount, nonceId }));
    }
};

template <typename... Ts>
std::vector<Element> table_line(Ts&&... ts)
{
    return { text(std::string(std::forward<Ts>(ts)))... };
}

template <typename... Ts>
std::vector<Element> highlight_table_line(bool highlight, Ts&&... ts)
{
    if (highlight)
        return { (text(std::string(std::forward<Ts>(ts))) | inverted)... };
    else
        return { (text(std::string(std::forward<Ts>(ts))))... };
}

inline Element render_balance(const std::optional<WartBalance>& b, GUI& gui)
{
    auto wart_text { [](std::string_view label, FundsDecimal w) { return text(std::string(label) + ": " + w.to_string() + " WART"); } };
    if (b)
        return vbox(wart_text("Total", b->total), wart_text("Locked", b->locked), wart_text("Free", b->free()));
    else
        return vbox(hbox(text("Total: "), gui.render_spinner()), hbox(text("Locked: "), gui.render_spinner()), hbox(text("Free: "), gui.render_spinner()));
}

struct WartTab : public MakeTab<WartTab>, public std::enable_shared_from_this<WartTab> {
    int selectedRow { 0 };
    Component btnTransfer;

    Element OnRender();
    void onTransfer() { }
    WartTab(GUI& gui)
        : MakeTab(gui, "Wart")
        , btnTransfer(Button("Transfer", [this]() { onTransfer(); }))
    {
        Add(Container::Vertical({ btnTransfer }));
    }
};
using MainTabs = Tabs<WalletTab, WartTab, AssetTab>;

} // namespace ui

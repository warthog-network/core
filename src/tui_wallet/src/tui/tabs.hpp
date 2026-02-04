#pragma once
#include "api/types/shared.hpp"
#include "callbacks.hpp"
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
#include "validators.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace ftxui;
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

struct NotificationData {
    std::string title, message;
};

struct NotificationPopup : public ui::Popup<NotificationPopup> {
private:
    NotificationData data;
    Element linesElement;
    Component btnOk;

public:
    NotificationPopup(GUI&, NotificationData data)
        : data(std::move(data))
        , btnOk(Button("OK", [this]() { closed = true; }))
    {
        Add(btnOk);
    }
    Element OnRender() override
    {
        return window(text(data.title),
            vbox(paragraph(data.message), btnOk->Render() | center));
    }
};

struct ConfirmationJob {
    NotificationData data;
    std::function<NotificationData()> work;
    void operator()()
    {
        try {
            data = work();
        } catch (std::runtime_error& e) {
            data.title = "Error";
            data.message = e.what();
        }
    }
    ~ConfirmationJob() = default;
};

struct ConfirmationPopup : public GUIComponent, public ui::Popup<ConfirmationPopup>{
    Component txdetails;
    Component btnCancel;
    Component btnConfirm;
    std::shared_ptr<NotificationPopup> resultPopup;
    bool submitting { false };

    ConfirmationPopup(GUI& gui, KVProperties txprops,
    std::function<NotificationData()> asyncJob, std::function<void()> syncDone);
    bool OnEvent(Event e) override
    {
        if (resultPopup)
            return resultPopup->OnEvent(e);
        return Popup::OnEvent(e);
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

struct SelectableList : public ComponentBase {
    size_t i { 0 };
    size_t n { 0 };
    virtual bool Focusable() const override
    {
        return i < n;
    }
    void update_element_count(size_t n)
    {
        this->n = n;
        if (n == 0) {
            i = 0;
        } else if (i >= n) {
            i = n - 1;
        }
    }
    virtual bool OnEvent(Event e) override
    {
        if (e == Event::k || e == Event::ArrowUp) {
            if (i > 0) {
                i -= 1;
                return true;
            }
        } else if (e == Event::j || e == Event::ArrowDown) {
            if (i + 1 < n) {
                i += 1;
                return true;
            }
        }
        return false;
    };
};

struct AssetSelectWindow : public ui::GUIComponent, public ComponentBase, public std::enable_shared_from_this<AssetSelectWindow> {

private:
    Component nameInput;
    Component hashInput;
    Component buttonCreate;
    bool canCreate { false };
    Component verticalButtons;
    std::vector<Component> buttons;
    bool clearCache { false };
    std::string namePrefix;
    std::string originalNamePrefix;
    std::string hashPrefix;
    void on_change();
    void on_select(const api_types::TokenListEntry&);
    void on_select_wart();
    void remove_buttons();

    void process_completions();
    auto& get_data();

public:
    AssetSelectWindow(GUI& gui);
    Element OnRender() override;
};

struct AssetSelectedWindow : public ui::GUIComponent, public ComponentBase, public std::enable_shared_from_this<AssetSelectedWindow> {

private:
    std::optional<AssetHash> prevSelectedHash;
    std::optional<AssetHash> currentSelectedHash;
    Component btnFork;
    Component btnTransfer;
    Component btnBuy;
    Component btnSell;
    Component btnLiquidityTransfer;
    Component btnDeposit;
    Component btnWithdraw;
    Component containerBalance;
    bool anything_selected() { return currentSelectedHash.has_value(); }
    bool nonwart_selected() { return anything_selected() && currentSelectedHash != AssetHash::WART; }
    void on_asset_transfer();
    void on_asset_swap(bool buy);
    void on_liquidity_transfer();
    void on_liquidity_farm(bool deposit);

public:
    AssetSelectedWindow(GUI& gui);
    Element OnRender() override;
};

struct AssetsTab : public MakeTab<AssetsTab> {
    //
private:
    std::shared_ptr<AssetSelectWindow> selectWindow;
    std::shared_ptr<AssetSelectedWindow> selectedWindow;

public:
    AssetsTab(GUI& gui);
};

struct WalletTab : public MakeTab<WalletTab> {

    WalletTab(GUI& gui)
        : MakeTab(gui, "Wallet")
    {
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

inline auto padded(const api::FundsBalance& b)
{
    auto nonfrac_digits = [](std::string n) {
        auto p { n.find('.') };
        return (p == n.npos ? n.size() : p);
    };
    auto totalStr { b.total.to_string() };
    auto lockedStr { b.locked.to_string() };
    auto freeStr { b.free().to_string() };
    auto totalDigits { nonfrac_digits(totalStr) };
    auto lockedDigits { nonfrac_digits(lockedStr) };
    auto freeDigits { nonfrac_digits(freeStr) };
    auto maxDigits = std::max(totalDigits, std::max(lockedDigits, freeDigits));
    struct Padded {
        std::string total;
        std::string locked;
        std::string free;
    };
    return Padded {
        .total = std::string(maxDigits - totalDigits, ' ') + totalStr,
        .locked = std::string(maxDigits - lockedDigits, ' ') + lockedStr,
        .free = std::string(maxDigits - freeDigits, ' ') + freeStr,
    };
}

inline Element render_balance(const std::optional<api::FundsBalance>& b, GUI& gui)
{
    auto wart_text { [](std::string_view label, const std::string& w) { return text(std::string(label) + ": " + w); } };
    if (b) {
        auto p { padded(*b) };
        return vbox(wart_text("Total ", p.total), wart_text("Locked", p.locked), wart_text("Free  ", p.free));
    } else
        return vbox(hbox(text("Total: "), gui.render_spinner()), hbox(text("Locked: "), gui.render_spinner()), hbox(text("Free: "), gui.render_spinner()));
}
inline Element render_balance(const std::optional<WartBalance>& b, GUI& gui)
{
    auto wart_text { [](std::string_view label, const std::string& w) { return text(std::string(label) + ": " + w + " WART"); } };
    if (b) {
        auto p { padded(*b) };
        return vbox(wart_text("Total ", p.total), wart_text("Locked", p.locked), wart_text("Free  ", p.free));
    } else
        return vbox(hbox(text("Total: "), gui.render_spinner()), hbox(text("Locked: "), gui.render_spinner()), hbox(text("Free: "), gui.render_spinner()));
}

struct LogTab : public MakeTab<LogTab> {
    int selectedRow { 0 };
    Element OnRender();
    LogTab(GUI& gui)
        : MakeTab(gui, "Log")
    {
    }
};

struct RequestsLogTab : public MakeTab<RequestsLogTab> {
    int selectedRow { 0 };
    Element OnRender();
    RequestsLogTab(GUI& gui)
        : MakeTab(gui, "Requests")
    {
    }
};

using MainTabs = Tabs<WalletTab, AssetsTab, RequestsLogTab>;

} // namespace ui

#include "tabs.hpp"
#include "global.hpp"
#include "popups.hpp"
#include "time_format.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ranges>

namespace ui {
ScreenInteractive& GUIComponent::extract_screen(GUI& gui)
{
    return GUIComponent(gui).gui_screen();
};

RootComponent& GUIComponent::extract_root(GUI& gui) { return *gui._root; };

ScreenInteractive& GUIComponent::gui_screen() const { return gui.screen; }
RootComponent& GUIComponent::gui_root() const { return *gui._root; }

ConfirmationPopup::ConfirmationPopup(
    GUI& gui, KVProperties txprops,
    std::function<NotificationData()> asyncJob, std::function<void()> syncDone)
    : GUIComponent(gui)
    , txdetails(TransactionDetails(std::move(txprops)))
    , btnCancel(Button(
          "Cancel", [&]() { closed = true; }, ButtonRoundOption()))
{
    btnConfirm = Button(
        "Confirm",
        [this, job = std::move(asyncJob), done = std::move(syncDone)] mutable {
            submitting = true;
            std::thread { [job = std::move(job), done = std::move(done), popup = shared_from_this()] mutable {
                auto data {
                    [&] -> NotificationData {
                    try {
                        return job();
                    }catch(std::runtime_error& e) {
                            return {"Error", e.what()};
                    } }()
                };
                popup->gui_screen().Post([popup = std::move(popup), data = std::move(data), done = std::move(done)] mutable {
                    popup->make_popup<NotificationPopup>(std::move(data));
                    popup->closed = true;
                    done();
                });
            } }.detach();
        },
        ButtonRoundOption());
    Add(Container::Vertical(
        { txdetails, Container::Horizontal({ btnCancel, btnConfirm }) }));
    btnCancel->TakeFocus();
}

void AssetSelectWindow::on_change()
{
    auto valid_name {
        [](const std::string& s) -> bool {
            return s.length() <= 5 && std::ranges::all_of(s, [](char c) { return std::isalnum(c); });
        }
    };
    if (!valid_name(namePrefix)) {
        namePrefix = originalNamePrefix;
    } else {
        originalNamePrefix = namePrefix;
        canCreate = namePrefix.length() > 0;
    }
    clearCache = true;
}
namespace {
auto PlainButton(std::string label, auto&& on_click)
{

    ButtonOption option;
    option.transform = [](const EntryState& s) {
        auto element = text(s.label);
        if (s.focused) {
            element = element | bold | inverted;
        }
        return element;
    };
    return Button(label, std::forward<decltype(on_click)>(on_click), option);
}
}
AssetSelectWindow::AssetSelectWindow(GUI& gui)
    : ui::GUIComponent(gui)
    , nameInput(Input([&] {
        InputOption o;
        o.multiline = false;
        o.content = &namePrefix;
        o.on_change = [&] { on_change(); };
        return o;
    }()))
    , hashInput(Input([&] {
        InputOption o;
        o.multiline = false;
        o.content = &hashPrefix;
        o.on_change = [&] { on_change(); };
        return o;
    }()))
    , buttonCreate(Maybe(PlainButton("Create", []() {}), &canCreate))
    , verticalButtons(Container::Vertical({}))

{
    Add(Container::Vertical({ Container::Horizontal({ nameInput, buttonCreate }), hashInput, verticalButtons }));
}

auto& AssetSelectWindow::get_data()
{
    bool forceReload = clearCache;
    clearCache = false;
    return global::data_interface().token_complete(forceReload, [w = weak_from_this()]() { 
        if (auto p{w.lock()}) 
            p->process_completions(); }, namePrefix, hashPrefix);
}

void AssetSelectWindow::remove_buttons()
{
    verticalButtons->DetachAllChildren();
}
namespace {
const std::string wartName("WART");
const std::string wartHash("0000000000000000000000000000000000000000000000000000000000000000");
}

void AssetSelectWindow::on_select_wart()
{
    gui.selectedAsset = AssetInfo(
        wartName,
        AssetHash::WART,
        TokenPrecision::WART);
}
void AssetSelectWindow::on_select(const api_types::TokenListEntry& e)
{
    gui.selectedAsset = AssetInfo(
        e.name,
        AssetHash::parse_throw(e.hash),
        TokenPrecision(e.precision));
}

void AssetSelectWindow::process_completions()
{
    using namespace std;
    remove_buttons();
    auto& data { get_data() };
    std::string upperPrefix;
    std::ranges::transform(namePrefix, std::back_inserter(upperPrefix), [](unsigned char ch) { return std::toupper(ch); });
    if (wartName.starts_with(upperPrefix) && wartHash.starts_with(hashPrefix)) {
        verticalButtons->Add(PlainButton(std::format("{} {}", wartName, wartHash), [this] { on_select_wart(); }));
    }
    if (data) {
        for (auto& e : data->entries) {
            verticalButtons->Add(PlainButton(std::format("{} {}", e.name, e.hash), [this, e] { on_select(e); }));
        }
    }

    redraw_lambda()();
}

Element AssetSelectWindow::OnRender()
{
    auto renderedButtons { [&] {
        if (auto d { get_data() }) {
            if (verticalButtons->ChildCount() == 0) {
                return text("No assets found"), text("No assets found");
            }
            return verticalButtons->Render();
        } else
            return render_spinner();
    }() };
    return window(text("Select Asset"), vbox(hbox(text("Name: "), nameInput->Render(), buttonCreate), hbox(text("Hash: "), hashInput->Render()), renderedButtons));
}

// namespace{
// ButtonOption colored_button(ftxui::Color c){
//     ButtonOption o{ButtonOption::Simple()};
//     // o.animated_colors
//
// }

// }
AssetSelectedWindow::AssetSelectedWindow(GUI& gui)
    : ui::GUIComponent(gui)
    , btnFork { Maybe(Button("Fork", [] {}), [&] { return anything_selected(); }) }
    , btnTransfer { Maybe(Button("Transfer", [&] { on_asset_transfer(); }), [&] { return anything_selected(); }) }
    , btnBuy { Maybe(Button("  Buy  ", [&] { on_asset_swap(true); }, ButtonOption::Animated(ftxui::Color::GreenLight)), [&] { return nonwart_selected(); }) }
    , btnSell { Maybe(Button("  Sell  ", [&] { on_asset_swap(false); }, ButtonOption::Animated(ftxui::Color::RedLight)), [&] { return nonwart_selected(); }) }
    , btnLiquidityTransfer { Maybe(Button("Transfer", [&] { on_liquidity_transfer(); }), [&] { return nonwart_selected(); }) }
    , btnDeposit { Maybe(Button("Deposit", [&] { on_liquidity_farm(true); }, ButtonOption::Animated(ftxui::Color::GreenLight)), [&] { return nonwart_selected(); }) }
    , btnWithdraw { Maybe(Button("Withdraw", [&] { on_liquidity_farm(false); }, ButtonOption::Animated(ftxui::Color::RedLight)), [&] { return nonwart_selected(); }) }
    , containerBalance(Container::Horizontal({ btnTransfer, btnBuy, btnSell }))
{
    Add(Container::Vertical({ Container::Horizontal({ btnFork }), containerBalance,
        Container::Horizontal({ btnLiquidityTransfer, btnDeposit, btnWithdraw }) }));
}
void AssetSelectedWindow::on_asset_transfer()
{
    if (auto a { gui.selectedAsset })
        make_popup<TransferPopup>(a->token(false));
}

void AssetSelectedWindow::on_asset_swap(bool buy)
{
    if (auto a { gui.selectedAsset };
        a.has_value() && !a->hash.is_wart())
        make_popup<SwapPopup>(*a, buy);
}

void AssetSelectedWindow::on_liquidity_transfer()
{
    if (auto a { gui.selectedAsset }) {
        make_popup<TransferPopup>(a->token(true));
    }
}

void AssetSelectedWindow::on_liquidity_farm(bool deposit)
{
    if (auto a { gui.selectedAsset })
        make_popup<FarmPopup>(*a, deposit);
}
Element AssetSelectedWindow::OnRender()
{
    auto windowContent = [&] {
    if (auto& a { gui.selectedAsset }) {
            prevSelectedHash = currentSelectedHash;
            currentSelectedHash = a->hash;
            bool fresh{prevSelectedHash != currentSelectedHash};
            if (fresh) {
                btnFork->OnFocusableChanged();
                btnTransfer->OnFocusableChanged();
                btnBuy->OnFocusableChanged();
                btnSell->OnFocusableChanged();
                btnLiquidityTransfer->OnFocusableChanged();
                btnDeposit->OnFocusableChanged();
                btnWithdraw->OnFocusableChanged();
            }

            auto balance { global::globals().dataInterface.asset_balance(fresh, redraw_lambda(), a->hash ) };
        std::vector<std::vector<Element>>
            initArg {
                { text("Name "), text(a->name) },
                { text("Hash "), text(a->hash.hex_string()) },
                { text("Precision "), text(std::to_string(a->precision.value())) },
                { text(""), hbox(btnFork->Render()) },
                { text("Balance "), vbox(render_balance(balance, gui),hbox(btnTransfer->Render(), btnBuy->Render(), btnSell->Render())) },
            };
            if (nonwart_selected()) {
                    auto lBalance { global::globals().dataInterface.liquidity_balance(fresh, redraw_lambda(), a->hash ) };
                initArg.push_back(
                { text("Liquidity "), vbox(render_balance(lBalance, gui),hbox(btnLiquidityTransfer->Render(), btnDeposit->Render(), btnWithdraw->Render())) });
                
            }
        ftxui::Table table(std::move(initArg));
        table.SelectColumn(0).BorderRight(EMPTY);
        table.SelectColumn(1).BorderRight(EMPTY);
        table.SelectColumn(2).BorderRight(EMPTY);
        return table.Render();
    } else {
        return vbox(text("No asset selected"));
    } }();
    return window(text("Selected Asset"), windowContent);
}

AssetsTab::AssetsTab(GUI& gui)
    : MakeTab<AssetsTab>(gui, "Assets")
    , selectWindow(Make<AssetSelectWindow>(gui))
    , selectedWindow(Make<AssetSelectedWindow>(gui))
{
    Add(Container::Vertical({ selectWindow, selectedWindow }));
}

Element LogTab::OnRender()
{
    std::vector<std::vector<Element>>
        initArg {
            table_line("Time", "Log"),
            // highlight_table_line(selectedRow == 0, "0x0000000000000000000000000000000000000000000000000000000000000000", "Warthog", "0.00000000", "WART"),
        };
    for (auto& m : global::log().messages()) {
        initArg.push_back({ text("A"), text(m) });
    };
    ftxui::Table table(std::move(initArg));
    table.SelectRow(0).BorderBottom(EMPTY);
    table.SelectColumn(0).BorderRight(EMPTY);
    table.SelectColumn(1).BorderRight(EMPTY);
    table.SelectColumn(2).BorderRight(EMPTY);
    return table.Render();
}
Element RequestsLogTab::OnRender()
{
    std::vector<std::vector<Element>>
        initArg {
            table_line("Time", "State", "Request"),
            // highlight_table_line(selectedRow == 0, "0x0000000000000000000000000000000000000000000000000000000000000000", "Warthog", "0.00000000", "WART"),
        };
    auto& messages { global::request_log().messages() };
    for (auto& m : std::ranges::reverse_view(messages)) {
        auto state { m->state() };
        auto success { [&]() {
            auto s { state.success };
            if (s.has_value()) {
                if (s.value()) {
                    return text("Success");
                } else {
                    return text("Failed");
                }
            }
            return render_spinner();
        }() };

        initArg.push_back({ text(format_duration(state.elapsed)), success, text(m->message()) });
    };
    ftxui::Table table(std::move(initArg));
    table.SelectRow(0).BorderBottom(EMPTY);
    table.SelectColumn(0).BorderRight(EMPTY);
    table.SelectColumn(1).BorderRight(EMPTY);
    table.SelectColumn(2).BorderRight(EMPTY);
    return table.Render();
}
} // namespace ui

#include "tabs.hpp"
#include "global.hpp"
#include "popups.hpp"
#include "time_format.hpp"
#include <cmath>
#include <iostream>
#include <ranges>
#include <set>

namespace ui {
ScreenInteractive& GUIComponent::extract_screen(GUI& gui)
{
    return GUIComponent(gui).gui_screen();
};

RootComponent& GUIComponent::extract_root(GUI& gui) { return *gui.root; };

auto ConfirmationPopup::result_cb()
{
    auto& screen { gui_screen() };
    return [weakgui = gui.weak_from_this(), this, &screen](std::string title,
               std::string message) {
        auto pgui { weakgui.lock() };
        if (pgui) {
            screen.Post([this, &screen, pgui = std::move(pgui), title,
                            message = std::move(message)]() {
                closed = true;
                make_popup<NotificationPopup>(std::move(title), std::move(message));
                screen.RequestAnimationFrame();
            });
        }
    };
}

ScreenInteractive& GUIComponent::gui_screen() const { return gui.screen; }
RootComponent& GUIComponent::gui_root() const { return *gui.root; }

ConfirmationPopup::ConfirmationPopup(
    GUI& gui, KVProperties txprops,
    onconfirm_generator_t onConfirmGenerator)
    : GUIComponent(gui)
    , txdetails(TransactionDetails(std::move(txprops)))
    , btnCancel(Button(
          "Cancel", [&]() { closed = true; }, ButtonRoundOption()))
    , btnConfirm(Button(
          "Confirm",
          [&, onConfirm = onConfirmGenerator(result_cb())]() {
              submitting = true;
              onConfirm();
          },
          ButtonRoundOption()))
{
    Add(Container::Vertical(
        { txdetails, Container::Horizontal({ btnCancel, btnConfirm }) }));
    btnCancel->TakeFocus();
}

AssetControlTab::AssetControlTab(GUI& gui)
    : MakeTab(gui, "Control")
    , btnTransferAsset(Button("Transfer", [&]() { on_asset_transfer(); }))
    , btnSwap(Button("Swap", [&]() { on_asset_swap(); }))
    , btnTransferLiquidity(
          Button("Transfer", [&]() { on_liquidity_transfer(); }))
    , btnFarm(Button("Farm", [&]() { on_liquidity_farm(); }))
{
    Add(Container::Horizontal(
        { Container::Vertical({ btnTransferAsset, btnSwap }),
            Container::Vertical({ btnTransferLiquidity, btnFarm }) }));
}

void AssetControlTab::on_asset_transfer()
{
    make_popup<TransferPopup>(TokenInfo::DEMO);
}

void AssetControlTab::on_asset_swap()
{
    make_popup<SwapPopup>(AssetInfo::demo());
}

void AssetControlTab::on_liquidity_transfer()
{
    auto a { TokenInfo::DEMO };
    a.spec.isLiquidity = true;
    make_popup<TransferPopup>(std::move(a));
}

void AssetControlTab::on_liquidity_farm()
{
    make_popup<FarmPopup>(AssetInfo::demo());
}

void AssetSelectTab::on_change()
{
    clearCache = true;
}
AssetSelectTab::AssetSelectTab(GUI& gui)
    : MakeTab(gui, "Select")
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
    , verticalButtons(Container::Vertical({}))
    , selectTableDummy { Make<SelectableList>() }

{
    Add(Container::Vertical({ nameInput, hashInput, verticalButtons }));
}

auto& AssetSelectTab::get_data()
{
    bool forceReload = clearCache;
    clearCache = false;
    return global::data_interface().token_complete(forceReload, [w = weak_from_this()]() { 
        if (auto p{w.lock()}) 
            p->process_completions(); }, namePrefix, hashPrefix);
}

void AssetSelectTab::remove_buttons()
{
    verticalButtons->DetachAllChildren();
}

void AssetSelectTab::on_select(const api_types::TokenListEntry& e)
{
    std::cerr << e.hash << std::endl;
}

void AssetSelectTab::process_completions()
{
    using namespace std;
    remove_buttons();
    auto& data { get_data() };
    if (data) {
        for (auto& e : data->entries) {
            verticalButtons->Add(Button(std::format("{} {}", e.name, e.hash), [this, e] { on_select(e); }));
        }
    }

    auto n { data ? data->entries.size() : 0 };
    selectTableDummy->update_element_count(n);
    redraw_lambda()();
}

Element AssetSelectTab::OnRender()
{
    auto renderedButtons { [&] {
        if (auto d { get_data() }) {
            if (d->entries.size() == 0) {
                return text("No assets found"), text("No assets found");
            }
            return verticalButtons->Render();
        } else
            return render_spinner();
    }() };
    return window(text("Select Asset"), vbox(hbox(text("Name: "), nameInput->Render()), hbox(text("Hash: "), hashInput->Render()), renderedButtons));
}

AssetCreateTab::AssetCreateTab(GUI& gui)
    : MakeTab(gui, "Create")
    , btnCreateNew(Button("New", [&]() { on_create_new(); }))
    , btnCreateFork(Button("Fork (Soon)", [&]() {}))
{
    Add(Container::Horizontal(
        { Container::Vertical({ btnCreateNew, btnCreateFork }) }));
}
void AssetCreateTab::on_create_new()
{
}
void AssetCreateTab::on_create_fork()
{
}
void WartTab::on_transfer()
{
    make_popup<TransferPopup>(TokenInfo::WART);
}
Element WartTab::OnRender()
{
    auto wart { global::globals().dataInterface.get_wart_balance(redraw_lambda()) };
    return vbox(render_balance(wart, gui), btnTransfer->Render());
    //
    //     std::vector<std::vector<Element>>
    //         initArg {
    //             table_line("Token", "Name", "Balance", "Ticker"),
    //             highlight_table_line(selectedRow == 0, "0x0000000000000000000000000000000000000000000000000000000000000000", "Warthog", "0.00000000", "WART"),
    //             { text("0x0000000000000000000000000000000000000000000000000000000000000000"), text("Warthog"), text("0.00000000"), text("WART") },
    //             { balance->Render(), text("World") },
    //             { amount->Render(), text("World") },
    //             { nonceId->Render(), text("World") }
    //         };
    // ftxui::Table table(std::move(initArg));
    // table.SelectRow(0).BorderBottom(EMPTY);
    // table.SelectColumn(0).BorderRight(EMPTY);
    // table.SelectColumn(1).BorderRight(EMPTY);
    // table.SelectColumn(2).BorderRight(EMPTY);
    // return table.Render();
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

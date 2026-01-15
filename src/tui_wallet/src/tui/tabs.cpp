#include "tabs.hpp"
#include "global.hpp"
#include "popups.hpp"
#include "root.hpp"

namespace ui {
ScreenInteractive& GUIComponent::extract_screen(GUI& gui)
{
    return GUIComponent(gui).gui_screen();
};

RootComponent& GUIComponent::extract_root(GUI& gui) { return *gui.root; };

auto ConfirmationComponentBase::result_cb()
{
    auto& screen { gui_screen() };
    return [weakgui = gui.weak_from_this(), this, &screen](std::string title,
               std::string message) {
        auto pgui { weakgui.lock() };
        if (pgui) {
            screen.Post([this, &screen, pgui = std::move(pgui), title,
                            message = std::move(message)]() {
                closed = true;
                extract_root(*pgui).popup_notification(title, message);
                screen.RequestAnimationFrame();
            });
        }
    };
}

ScreenInteractive& GUIComponent::gui_screen() const { return gui.screen; }
RootComponent& GUIComponent::gui_root() const { return *gui.root; }

void RootComponent::popup_notification(std::string title, std::string message)
{
    add_popup(Make<NotificationPopupBase>(std::move(title), std::move(message)));
}

void RootComponent::popup_confirmation(KVProperties txprops,
    onconfirm_generator_t handler)
{
    auto confirm { Make<ConfirmationComponentBase>(gui, std::move(txprops),
        std::move(handler)) };
    add_popup(std::move(confirm));
}

ConfirmationComponentBase::ConfirmationComponentBase(
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
    gui_root().add_popup(Make<TransferPopup>(gui, AssetNameHash::demo(), false));
}

void AssetControlTab::on_asset_swap()
{
    gui_root().add_popup(Make<SwapPopup>(gui, AssetNameHash::demo()));
}

void AssetControlTab::on_liquidity_transfer()
{
    gui_root().add_popup(Make<TransferPopup>(gui, AssetNameHash::demo(), true));
}
void AssetControlTab::on_liquidity_farm()
{
    gui_root().add_popup(Make<FarmPopup>(gui, AssetNameHash::demo()));
}

AssetSelectTab::AssetSelectTab(GUI& gui)
    : MakeTab(gui, "Select")
{
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
Element WartTab::OnRender()
{
    auto wart{global::globals().dataInterface.get_wart_balance(redraw_lambda())};
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
} // namespace ui

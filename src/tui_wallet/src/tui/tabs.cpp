#include "tabs.hpp"
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

SpinnerWorker::SpinnerWorker(ScreenInteractive& screen)
    : screen(screen)
    , t([this]() {
        using namespace std;
        std::unique_lock l(m);
        while (true) {
            if (cv.wait_for(l, std::chrono::milliseconds(300), [&]() { return this->stop_requestd; }))
                break;
            spinnerStep += 1;
            this->screen.PostEvent(Event::Custom);
        }
    })
{
}

auto SpinnerFactory::get_handle() const -> SpinnerHandle
{
    if (!handle)
        handle = std::make_shared<SpinnerWorker>(screen);
    return { handle };
}
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
    , spinner(gui.get_spinner())
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
    , spinner(gui.get_spinner())
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
    , spinner(gui.get_spinner())
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
} // namespace ui

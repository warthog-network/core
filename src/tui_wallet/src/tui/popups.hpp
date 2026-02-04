#pragma once
#include "general/compact_uint.hpp"
#include "gui.hpp"
#include "popup.hpp"
#include "validated_input.hpp"

namespace ui {

struct TransferPopup : public GUIComponent,
                       public Popup<TransferPopup> {
private:
    TokenInfo token;
    std::shared_ptr<ui::LabeledValidatedBase> amount;
    std::shared_ptr<ui::LabeledValidatedBase> toAddr;
    std::shared_ptr<ui::LabeledValidatedBase> fee;
    std::shared_ptr<ui::LabeledValidatedBase> nonceId;
    Component btnCancel;
    Component btnCreate;

public:
    Element OnRender() override
    {
        using namespace std::string_literals;
        auto content = [&]() {
            if (token.spec.isLiquidity) {
                amount->label = "Amount ("s + token.liquidity_name() + "): ";
                return vbox({ text("Pool: "s + token.market()),
                    text("Token: " + token.to_string()), toAddr, amount,
                    fee, nonceId });
            } else {
                amount->label = "Amount ("s + token.assetName + "): ";
                return vbox({ text("Token: " + token.to_string()), toAddr, amount, fee,
                    nonceId });
            }
        }();
        auto title { "New "s + (token.spec == api::TokenSpec::WART ? "WART" : token.spec.isLiquidity ? "Liquidity"
                                                                                                     : "Asset")
            + " Transfer"s };
        return vbox({ window(text(title), content),
            hbox(btnCancel, btnCreate->Render()) | center });
    }
    void on_create();
    void on_cancel();
    TransferPopup(GUI& gui, TokenInfo tokenInfo);
};
// template<

struct SwapPopup : public GUIComponent,
                   public Popup<SwapPopup> {
private:
    AssetInfo asset;
    std::vector<std::string> swap_directions;
    int side_selected = 0;

    std::shared_ptr<ui::LabeledValidatedBase> amount;
    std::shared_ptr<ui::LabeledValidatedBase> limit;
    std::shared_ptr<ui::LabeledValidatedBase> fee;
    Component toggle;
    Component btnCancel;
    Component btnCreate;
    bool is_buy() const { return side_selected == 0; }

public:
    Element OnRender() override;
    void on_create();
    void on_cancel();
    SwapPopup(GUI& gui, AssetInfo, bool buy);
};
struct FarmPopup : public GUIComponent, public Popup<FarmPopup>{
private:
    AssetInfo asset;
    std::vector<std::string> liquidity_actions;
    int side_selected = 0;

    std::shared_ptr<ui::LabeledValidatedBase> wart;
    std::shared_ptr<ui::LabeledValidatedBase> base;
    std::shared_ptr<ui::LabeledValidatedBase> limit;
    std::shared_ptr<ui::LabeledValidatedBase> fee;
    Component toggle;
    Component btnCancel;
    Component btnCreate;
    bool is_deposit() const { return side_selected == 0; }

public:
    Element OnRender() override
    {
        base->label = "Max. Amount (" + asset.name + "): ";
        return vbox(
            { window(text("Farm Liquidity"),
                  vbox({ text("Base Asset: " + asset.to_string()),
                      hbox(text("Liquidity action: "), toggle->Render()),
                      wart->Render(), limit->Render(), fee->Render() })),
                hbox(btnCancel, btnCreate->Render()) | center });
    }
    void on_create();
    void on_cancel();
    FarmPopup(GUI& gui, AssetInfo, bool deposit);
};

} // namespace ui

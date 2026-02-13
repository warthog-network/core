#pragma once
#include "gui.hpp"
#include "popup.hpp"
#include "validated_input.hpp"

namespace ui {

struct TransferPopup : public GUIComponent,
                       public Popup<TransferPopup> {
private:
    TokenInfo token;
    std::shared_ptr<ui::LabeledValidatedBase> editAmount;
    std::shared_ptr<ui::LabeledValidatedBase> editToAddr;
    std::shared_ptr<ui::LabeledValidatedBase> editFee;
    std::shared_ptr<ui::LabeledValidatedBase> editNonceId;
    Component btnCancel;
    Component btnCreate;

public:
    Element OnRender() override
    {
        using namespace std::string_literals;
        auto content = [&]() {
            if (token.spec.isLiquidity) {
                editAmount->label = "Amount ("s + token.liquidity_name() + "): ";
                return vbox({ text("Pool: "s + token.market()),
                    text("Token: " + token.to_string()), editToAddr, editAmount,
                    editFee, editNonceId });
            } else {
                editAmount->label = "Amount ("s + token.assetName + "): ";
                return vbox({ text("Token: " + token.to_string()), editToAddr, editAmount, editFee,
                    editNonceId });
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

    std::shared_ptr<ui::LabeledValidatedBase> editAmount;
    std::shared_ptr<ui::LabeledValidatedBase> editLimit;
    std::shared_ptr<ui::LabeledValidatedBase> editFee;
    std::shared_ptr<ui::LabeledValidatedBase> editNonceId;
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
    std::shared_ptr<ui::LabeledValidatedBase> editWart;
    Component maybeWart;
    std::shared_ptr<ui::LabeledValidatedBase> editBase;
    std::shared_ptr<ui::LabeledValidatedBase> editFee;
    std::shared_ptr<ui::LabeledValidatedBase> editNonceId;
    Component toggle;
    Component btnCancel;
    Component btnCreate;
    bool is_deposit() const { return side_selected == 0; }

public:
    Element OnRender() override;
    void on_create();
    void on_cancel();
    FarmPopup(GUI& gui, AssetInfo, bool deposit);
};

} // namespace ui

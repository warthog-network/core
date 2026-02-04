#include "popups.hpp"
#include "general/funds.hpp"
#include "tabs.hpp"
#include "transaction.hpp"
#include "validators.hpp"
namespace ui {
namespace {

auto format_token_caption(api::TokenSpec spec, std::string name)
{
    return std::format("{} ({})", spec.to_string(), name);
}

}

void TransferPopup::on_cancel() { closed = true; }

auto work {
    [] -> NotificationData {
        return { "Hello", "world" };
    }
};

void TransferPopup::on_create()
{
    auto allValid { amount->valid && toAddr->valid && fee->valid && nonceId->valid };
    if (!allValid)
        return;

    auto properties { KVProperties {
        .title { "Transfer" },
        .entries {
            { "Token ", token.to_string() },
            { "Amount (" + token.pretty_name() + ") ", amount.get()->content },
            { "Destination ", toAddr.get()->content },
            { "Fee (WART) ", fee.get()->content },
            { "NonceId ", nonceId.get()->content },
        } } };
    if (token.spec.isLiquidity) {
        properties.entries.push_back(
            { "NOTE: ",
                "This transfer is for pool liquidity, not the actual asset!" });
    }

    //     [tr = shared_from_this()](result_cb_t cb) -> std::function<void()> {
    //         return [tr = std::move(tr), cb = std::move(cb)]() mutable {
    //             std::thread t([tr = std::move(tr), cb = std::move(cb)] {
    //                 tr->closed = true;
    //                 std::this_thread::sleep_for(std::chrono::seconds(2));
    //                 cb("Success", "Transaction was sent");
    //             });
    //             t.detach();
    //         };
    //     }
    // };
    make_popup<ConfirmationPopup>(std::move(properties), work, close_callback());
};

TransferPopup::TransferPopup(GUI& gui, TokenInfo token)
    : GUIComponent(gui)
    , token(std::move(token))
    , amount(ui::LabeledValidated("Amount:  ", FundsValidator(token.prec())))
    , toAddr(ui::LabeledValidated("Destination: ", address_validator))
    , fee(ui::LabeledValidated("Fee (WART): ", wart_validator))
    , nonceId(ui::LabeledValidated("NonceId: ", nonce_id_validator))
    , btnCancel(Button("Cancel", [&]() { this->on_cancel(); }))
    , btnCreate(Button("Create", [&]() { this->on_create(); }))
{
    Add(Container::Vertical({ toAddr, amount, fee, nonceId,
        Container::Horizontal({ btnCancel, btnCreate }) }));
}

void SwapPopup::on_create()
{
    auto allValid { amount->valid && limit->valid && fee->valid };
    if (!allValid)
        return;
    auto assetCaption { format_token_caption(api::TokenSpec(asset.hash, false), asset.name) };
    auto wartCaption { format_token_caption(api::TokenSpec::WART, "Wart") };

    auto properties { KVProperties {
        .title { "Swap" },
        .entries {
            { "From Token ",
                is_buy() ? wartCaption : assetCaption },
            { "To Token ",
                is_buy() ? assetCaption : wartCaption },
            { "Amount", amount.get()->content },
            { "Limit Price ", limit.get()->content },
        } } };

    // onconfirm_generator_t generator {
    //     [tr = shared_from_this()](result_cb_t cb) -> std::function<void()> {
    //         return [tr = std::move(tr), cb = std::move(cb)]() mutable {
    //             std::thread t([tr = std::move(tr), cb = std::move(cb)] {
    //                 tr->closed = true;
    //                 std::this_thread::sleep_for(std::chrono::seconds(2));
    //                 cb("Success", "Transaction was sent");
    //             });
    //             t.detach();
    //         };
    //     }
    // };
    make_popup<ConfirmationPopup>(std::move(properties), work, close_callback());
};

void SwapPopup::on_cancel() { closed = true; }
Element SwapPopup::OnRender()
{
    amount->set_validator(FundsValidator(is_buy() ? asset.precision : TokenPrecision::WART));
    limit->set_validator(LimitValidator(asset.precision, !is_buy()));
    amount->label = std::string("Amount (") + (is_buy() ? "WART" : asset.name) + "): ";
    limit->label = "Limit (" + std::string(is_buy() ? "MAX" : "MIN") + " Price): ";
    return vbox(
        { window(text("New Swap"),
              vbox({ text("Base Asset: " + asset.to_string()),
                  hbox(text("Swap direction: "), toggle->Render()),
                  amount->Render(), limit->Render(), fee->Render() })),
            hbox(btnCancel, btnCreate->Render()) | center });
}

SwapPopup::SwapPopup(GUI& gui, AssetInfo a, bool buy)
    : GUIComponent(gui)
    , asset(std::move(a))
    , swap_directions { "BUY " + asset.name + " WITH WART",
        "SELL " + asset.name + " FOR WART" }
    , side_selected(buy ? 0 : 1)
    , amount(ui::LabeledValidated("Amount:  "))
    , limit(ui::LabeledValidated("Limit Price:  "))
    , fee(ui::LabeledValidated("Fee (WART):  ", fee_validator))
    , toggle(Toggle(swap_directions, &side_selected))
    , btnCancel(Button("Cancel", [&]() { this->on_cancel(); }))
    , btnCreate(Button("Create", [&]() { this->on_create(); }))
{
    Add(Container::Vertical({ toggle, amount, limit, fee,
        Container::Horizontal({ btnCancel, btnCreate }) }));
}
void FarmPopup::on_create()
{
    auto allValid { wart->valid && base->valid && limit->valid && fee->valid };
    // if (!allValid)
    //     return;
    auto properties { KVProperties {
        .title { "Farm" },
        .entries {
            { "From Token ",
                "95ae6efb2f4fe5e4fd3a5b21df7f755f878383610505fe64 (WART)" },
            { "To Token ",
                "95ae6efb2f4fe5e4fd3a5b21df7f755f878383610505fe64 (WART)" },
            { "Amount", wart.get()->content },
            { "Limit Price ", limit.get()->content },
        } } };

    // onconfirm_generator_t generator {
    //     [tr = shared_from_this()](result_cb_t cb) -> std::function<void()> {
    //         return [tr = std::move(tr), cb = std::move(cb)]() mutable {
    //             std::thread t([tr = std::move(tr), cb = std::move(cb)] {
    //                 tr->closed = true;
    //                 std::this_thread::sleep_for(std::chrono::seconds(2));
    //                 cb("Success", "Transaction was sent");
    //             });
    //             t.detach();
    //         };
    //     }
    // };
    make_popup<ConfirmationPopup>(std::move(properties), work, close_callback());
};
void FarmPopup::on_cancel() { closed = true; }

FarmPopup::FarmPopup(GUI& gui, AssetInfo a, bool deposit)
    : GUIComponent(gui)
    , asset(std::move(a))
    , liquidity_actions { "DEPOSIT LIQUIDITY",
        "WITHDRAW LIQUIDITY" }
    , side_selected(deposit ? 0 : 1)
    , wart(ui::LabeledValidated("Max. Amount (WART):  ", wart_validator))
    , base(ui::LabeledValidated("", FundsValidator(a.precision)))
    , limit(ui::LabeledValidated("Limit Price:  ", LimitValidator(a.precision, false)))
    , fee(ui::LabeledValidated("Fee (WART):  ", fee_validator))
    , toggle(Toggle(liquidity_actions, &side_selected))
    , btnCancel(Button("Cancel", [&]() { this->on_cancel(); }))
    , btnCreate(Button("Create", [&]() { this->on_create(); }))
{

    Add(Container::Vertical({ toggle, wart, limit, fee,
        Container::Horizontal({ btnCancel, btnCreate }) }));
}
} // namespace ui

#include "popups.hpp"
#include "general/funds.hpp"
#include "global.hpp"
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

// auto work {
//     [] -> NotificationData {
//         return { "Hello", "world" };
//     }
// };

void TransferPopup::on_create()
{
    // Parse text fields
    auto nId { try_parse<uint32_t>(editNonceId.get()->content) };
    auto compactFee { Wart::parse(editFee.get()->content).transform([](Wart w) { return CompactUInt::compact(w); }) };
    auto amt { Funds_uint64::parse(editAmount.get()->content, token.prec()).and_then([](Funds_uint64 f) { return f.nonzero(); }) };
    auto toAddress { Address::parse(editToAddr.get()->content) };

    // Wart itself has no liquidity
    if (token.spec.assetHash.is_wart() && token.spec.isLiquidity)
        return; // this should not happen and is not allowed

    if (!nId || !compactFee || !amt || !toAddress)
        return;

    auto properties { KVProperties {
        .title { "Transfer" },
        .entries {
            { "Token ", token.to_string() },
            { "Amount (" + token.pretty_name() + ") ", amt->to_decimal(token.prec()).to_string() },
            { "Destination ", toAddress->to_string() },
            { "Fee (WART) ", compactFee->to_string() },
            { "NonceId ", std::to_string(*nId) },
        } } };
    if (token.spec.isLiquidity) {
        properties.entries.push_back(
            { "NOTE: ",
                "This transfer is for pool liquidity, not the actual asset!" });
    }

    auto work {
        [token = token, nonceId = NonceId(*nId), compactFee = *compactFee, amount = *amt, toAddr = *toAddress] -> NotificationData {
            auto& drc { global::data_interface().retrieval_context() };
            auto ctx { drc.tx_create_context(nonceId, compactFee) };
            auto& s { token.spec };
            auto tx { [&] -> std::string {
                if (s.assetHash.is_wart()) {
                    return WartTransferCreate(ctx, toAddr, amount.as_wart());
                } else {
                    return TokenTransferCreate(ctx, s.assetHash, s.isLiquidity, toAddr, amount);
                }
            }() };
            auto hash { drc.endpoint.send_transaction(tx) };
            return { "Success", std::format("Transaction was sent.\r Transaction Hash: {}", serialize_hex(hash)) };
        }
    };
    make_popup<ConfirmationPopup>(std::move(properties), work, close_callback());
};

TransferPopup::TransferPopup(GUI& gui, TokenInfo token)
    : GUIComponent(gui)
    , token(std::move(token))
    , editAmount(ui::LabeledValidated("Amount:  ", NonzeroFundsValidator(token.prec())))
    , editToAddr(ui::LabeledValidated("Destination: ", address_validator))
    , editFee(ui::LabeledValidated("Fee (WART): ", wart_validator))
    , editNonceId(ui::LabeledValidated("NonceId: ", nonce_id_validator))
    , btnCancel(Button("Cancel", [&]() { this->on_cancel(); }))
    , btnCreate(Button("Create", [&]() { this->on_create(); }))
{

    editToAddr->content = "0000000000000000000000000000000000000000de47c9b2";
    editToAddr->validate();
    Add(Container::Vertical({ editToAddr, editAmount, editFee, editNonceId,
        Container::Horizontal({ btnCancel, btnCreate }) }));
}

void SwapPopup::on_create()
{
    auto allValid { editAmount->valid && editLimit->valid && editFee->valid };
    if (!allValid)
        return;
    auto assetCaption { format_token_caption(api::TokenSpec(asset.hash, false), asset.name) };
    auto wartCaption { format_token_caption(api::TokenSpec::WART, "Wart") };

    auto nId { try_parse<uint32_t>(editNonceId.get()->content) };
    auto compactFee { Wart::parse(editFee.get()->content).transform([](Wart w) { return CompactUInt::compact(w); }) };
    auto amt { Funds_uint64::parse(editAmount.get()->content, asset.precision).and_then([](Funds_uint64 f) { return f.nonzero(); }) };
    auto l { Price_uint64::from_string_adjusted(editLimit.get()->content, asset.precision) };

    if (!nId || !compactFee || !amt || !l)
        return;

    auto properties { KVProperties {
        .title { "Swap" },
        .entries {
            { "From Token ",
                is_buy() ? wartCaption : assetCaption },
            { "To Token ",
                is_buy() ? assetCaption : wartCaption },
            { "Amount", amt->to_decimal(asset.precision).to_string() },
            { "Limit Price ", std::to_string(l->to_double_adjusted(asset.precision)) },
            { "Fee (WART)", compactFee->to_string() } } } };
    auto work {
        [asset = asset, nonceId = NonceId(*nId), isBuy = is_buy(), compactFee = *compactFee, amount = *amt, limit = *l] -> NotificationData {
            auto& drc { global::data_interface().retrieval_context() };
            auto ctx { drc.tx_create_context(nonceId, compactFee) };
            auto& s { asset };
            auto hash { [&] {
                // DEFINE_CREATE_MESSAGE(LimitSwapCreate, ::block::labels::limitSwap, AssetHashEl, BuyEl, NonzeroAmountEl, LimitPriceEl)
                LimitSwapCreate tx(ctx, s.hash, isBuy, amount, limit);
                return drc.endpoint.send_transaction(tx);
            }() };
            return { "Success", std::format("Transaction was sent.\r Transaction Hash: {}", serialize_hex(hash)) };
        }
    };
    make_popup<ConfirmationPopup>(std::move(properties), work, close_callback());
};

void SwapPopup::on_cancel() { closed = true; }
Element SwapPopup::OnRender()
{
    editAmount->set_validator(NonzeroFundsValidator(is_buy() ? asset.precision : TokenPrecision::WART));
    editLimit->set_validator(LimitValidator(asset.precision, !is_buy()));
    editAmount->label = std::string("Amount (") + (is_buy() ? "WART" : asset.name) + "): ";
    editLimit->label = "Limit (" + std::string(is_buy() ? "MAX" : "MIN") + " Price): ";
    return vbox(
        { window(text("New Swap"),
              vbox({ text("Base Asset: " + asset.to_string()),
                  hbox(text("Swap direction: "), toggle->Render()),
                  editAmount->Render(), editLimit->Render(), editFee->Render(), editNonceId->Render() })),
            hbox(btnCancel, btnCreate->Render()) | center });
}

SwapPopup::SwapPopup(GUI& gui, AssetInfo a, bool buy)
    : GUIComponent(gui)
    , asset(std::move(a))
    , swap_directions { "BUY " + asset.name + " WITH WART",
        "SELL " + asset.name + " FOR WART" }
    , side_selected(buy ? 0 : 1)
    , editAmount(ui::LabeledValidated("Amount:  "))
    , editLimit(ui::LabeledValidated("Limit Price:  "))
    , editFee(ui::LabeledValidated("Fee (WART):  ", fee_validator))
    , editNonceId(ui::LabeledValidated("NonceId: ", nonce_id_validator))
    , toggle(Toggle(swap_directions, &side_selected))
    , btnCancel(Button("Cancel", [&]() { this->on_cancel(); }))
    , btnCreate(Button("Create", [&]() { this->on_create(); }))
{
    Add(Container::Vertical({ toggle, editAmount, editLimit, editFee, editNonceId,
        Container::Horizontal({ btnCancel, btnCreate }) }));
}
void FarmPopup::on_create()
{
    // editWart;
    // std::shared_ptr<ui::LabeledValidatedBase> editBase;
    // std::shared_ptr<ui::LabeledValidatedBase> editFee;
    // std::shared_ptr<ui::LabeledValidatedBase> editNonceId;

    auto precision { is_deposit() ? asset.precision : TokenPrecision::LIQUIDITY };
    auto amtWart { Wart::parse(editWart.get()->content) };
    auto amtBase { Funds_uint64::parse(editBase.get()->content, precision) };
    auto compactFee { Wart::parse(editFee.get()->content).transform([](Wart w) { return CompactUInt::compact(w); }) };
    auto nId { try_parse<uint32_t>(editNonceId.get()->content) };

    if ((is_deposit() && !amtWart) || !amtBase || !compactFee || !nId)
        return;

    KVProperties properties {
        .title { (is_deposit() ? "Deposit liquidity into " : "Withdraw liquidity from ") + asset.market() + " pool" },
        .entries { { "Base Asset: ", asset.to_string() },
            { "Pool: ", asset.market() } }
    };
    if (is_deposit()) {
        properties.entries.push_back({ "Amount (WART): ", amtWart->to_string() });
        properties.entries.push_back({ "Amount (" + asset.token(false).pretty_name() + "): ", amtBase->to_decimal(precision).to_string() });
    } else {
        properties.entries.push_back({ "Amount (" + asset.token(true).pretty_name() + "): ", amtBase->to_decimal(precision).to_string() });
    }
    properties.entries.push_back({ "Fee (WART): ", compactFee->to_string() });
    properties.entries.push_back({ "NonceId: ", std::to_string(*nId) });
    std::function<NotificationData()> work;
    if (is_deposit()) {
        // we must create a LiquidityDepositCreate
        work = [asset = asset, nonceId = NonceId(*nId), compactFee = *compactFee, amountWart = amtWart.value(), amountBase = *amtBase] -> NotificationData {
            auto& drc { global::data_interface().retrieval_context() };
            auto ctx { drc.tx_create_context(nonceId, compactFee) };
            auto& s { asset };
            auto hash { [&] {
                LiquidityDepositCreate tx(ctx, s.hash, amountBase, amountWart);
                return drc.endpoint.send_transaction(tx);
            }() };
            return { "Success", std::format("Transaction was sent.\r Transaction Hash: {}", serialize_hex(hash)) };
        };
    } else {
        auto nonzeroShares { amtBase->nonzero() };
        if (!nonzeroShares)
            return;
        // we must create a LiquidityWithdrawalCreate
        work = [asset = asset, nonceId = NonceId(*nId), compactFee = *compactFee, amountShares = *nonzeroShares] -> NotificationData {
            auto& drc { global::data_interface().retrieval_context() };
            auto ctx { drc.tx_create_context(nonceId, compactFee) };
            auto& s { asset };
            auto hash { [&] {
                // DEFINE_CREATE_MESSAGE(LimitSwapCreate, ::block::labels::limitSwap, AssetHashEl, BuyEl, NonzeroAmountEl, LimitPriceEl)
                LiquidityWithdrawalCreate tx(ctx, s.hash, amountShares);
                return drc.endpoint.send_transaction(tx);
            }() };
            return { "Success", std::format("Transaction was sent.\r Transaction Hash: {}", serialize_hex(hash)) };
        };
    }
    make_popup<ConfirmationPopup>(std::move(properties), work, close_callback());
};
void FarmPopup::on_cancel() { closed = true; }

FarmPopup::FarmPopup(GUI& gui, AssetInfo a, bool deposit)
    : GUIComponent(gui)
    , asset(std::move(a))
    , liquidity_actions { "DEPOSIT LIQUIDITY",
        "WITHDRAW LIQUIDITY" }
    , side_selected(deposit ? 0 : 1)
    , editWart(ui::LabeledValidated("Amount (WART): ",
          [this](std::string& wartContent) {
              if (is_deposit()) {
                  // if we are in deposit mode, then the restriction is that not both can be zero but one can be zero,
                  auto amtBase { Funds_uint64::parse(editBase.get()->content, asset.precision) };
                  auto amtWart { Wart::parse(wartContent) };
                  if (amtWart && amtBase) {
                      return !(amtWart->is_zero() && amtBase->is_zero());
                  }
                  // if not both fields can be parsed, we only declare as invalid if this itself cannot be parsed
                  return amtWart.has_value();
              }
              return true;
          }))
    , maybeWart(Maybe(editWart, [&] { return is_deposit(); }))
    , editBase(ui::LabeledValidated("", [this](std::string& s) -> bool {
        auto amtBase { Funds_uint64::parse(s, asset.precision) };
        if (is_deposit()) {
            // if we are in deposit mode, then the restriction is that not both can be zero but one can be zero,
            auto amtWart { Wart::parse(editWart.get()->content) };
            if (amtWart && amtBase) {
                return !(amtWart->is_zero() && amtBase->is_zero());
            }
            return amtBase.has_value();
        } else {
            // if we are in withdraw mode, then we only have 1 field (number of shares) which cannot be zero.
            return amtBase && !amtBase->is_zero();
        }
    }))
    , editFee(ui::LabeledValidated("Fee (WART):  ", fee_validator))
    , editNonceId(ui::LabeledValidated("NonceId: ", nonce_id_validator))
    , toggle(Toggle(liquidity_actions, &side_selected))
    , btnCancel(Button("Cancel", [&]() { this->on_cancel(); }))
    , btnCreate(Button("Create", [&]() { this->on_create(); }))
{

    Add(Container::Vertical({ toggle,
        maybeWart, editBase, editFee, editNonceId,
        Container::Horizontal({ btnCancel, btnCreate }) }));
}
Element FarmPopup::OnRender()
{
    editBase->label = "Amount (" + asset.token(!is_deposit()).pretty_name() + "): ";
    return vbox(
        { window(text("Farm Liquidity"),
              vbox({ text("Base Asset: " + asset.to_string()),
                  text("Pool: " + asset.market()),
                  hbox(text("Liquidity action: "), toggle->Render()),
                  maybeWart->Render(), editBase->Render(), editFee->Render(), editNonceId->Render() })),
            hbox(btnCancel, btnCreate->Render()) | center });
}
} // namespace ui

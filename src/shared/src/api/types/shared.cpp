#include "shared.hpp"

namespace api {
void Block::set_reward(block::WithHistoryId<block::Reward> r)
{
    if (actions.reward.has_value())
        throw std::runtime_error("Database error, each block can only have one reward transaction");
    actions.reward = r;
}

auto padded(const FundsBalance& b)
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

}

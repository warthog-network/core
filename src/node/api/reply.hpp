#pragma once
#include "general/fwd.hpp"
#include <string>
#include <variant>

struct HoldsString {
    std::string data;
    HoldsString(std::string data)
        : data(std::move(data))
    {
    }
};
struct JSONString: public HoldsString {
    using HoldsString::HoldsString;
    static constexpr const std::string_view content_type = "application/json; charset=utf-8";
};
struct HTMLString: public HoldsString {
    using HoldsString::HoldsString;
    static constexpr const std::string_view content_type = "text/html; charset=utf-8";
};

struct APIReply : public std::variant<JSONString, HTMLString> {
    using std::variant<JSONString, HTMLString>::variant;
    auto&& raw(this auto&& self)
    {
        return std::visit([](auto&& s) -> auto&& {
            return FWD(s).data;
        },
            FWD(self));
    }
    static APIReply HTML(std::string raw)
    {
        return HTMLString(std::move(raw));
    }
    static APIReply JSON(std::string raw)
    {
        return JSONString(std::move(raw));
    }
    std::string_view content_type() const
    {
        return std::visit([](auto& v) { return v.content_type; }, *this);
    }
};

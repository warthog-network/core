#include <string_view>

[[nodiscard]] constexpr inline std::pair<std::string_view, std::string_view> 
split(std::string_view s, char token) noexcept
{
    auto pos { s.find(token) };
    if (pos == s.npos)
        return { s, {} };
    return { s.substr(0, pos), s.substr(pos + 1) };
}

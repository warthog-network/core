#include "schema_aggregator.hpp"
#include "../html_escape.hpp"
#include <sstream>

struct HTMLAggregator {
    struct Formatter {
        bool plural = false;
        std::string format_first_word(std::string_view v) const
        {
            if (plural) {
                std::string out(" ");
                std::ranges::transform(v, std::back_inserter(out), [](unsigned char c) { return std::tolower(c); });
                out += "s";
                return out;
            }
            return std::string(v);
        }
        void reset()
        {
            plural = false;
        }
    } formatter;
    auto format_first_word(std::string_view v)
    {
        auto tmp { formatter.format_first_word(v) };
        formatter.reset();
        return tmp;
    }
    using schema_ptr = std::shared_ptr<glz::schema>;
    static constexpr glz::opts Opts {};
    static constexpr glz::opts options = glz::opts_write_type_info_off<decltype(Opts)> { { Opts } };

    std::stringstream data;
    HTMLAggregator()
    {
        data << R"(
<!DOCTYPE html>
<html>
    <head>
        <meta charset="UTF-8" />
        <meta name="viewport" content="width=device-width" />
        <title></title>
        <style>
          table, th, td {
            border-collapse: collapse;
            border: 1px solid;
          }
          section {
            transition: background-color 0.5s;
          }
          section:target {
            background-color: yellow;
          }
        </style>
    </head>
    <body>
    )";
    }
    static std::string gen_link(std::string_view name)
    {
        return std::format("<a href=\"#{}\">{}</a>", name, name);
    }
    auto normalize_name(std::string_view str)
    {
        size_t pos = str.find_last_of('/');

        if (pos == std::string::npos) {
            return html_escape(str);
        }
        return html_escape(str.substr(pos + 1));
    }
    bool add_object(const glz::schema& s)
    {
        if (s.properties) {
            data << format_first_word("Objct") << " with properties:";
            data << " <table>";
            for (auto [key, val] : s.properties.value()) {
                data << "<tr><td>" << key << "</td><td>";
                add_inner_entry(val);
                data << "</td></tr>";
            }
            data << "</table>";
        } else if (s.additionalProperties && std::holds_alternative<schema_ptr>(*s.additionalProperties)) {
            auto& item = std::get<schema_ptr>(*s.additionalProperties);
            data << format_first_word("Object") << " with values of type ";
            add_inner_entry(*item);
        } else {
            return false;
        }
        return true;
    }
    bool add_array(const glz::schema& s)
    {
        if (s.items.has_value() && std::holds_alternative<schema_ptr>(*s.items)) {
            data << format_first_word("Array") << " containing ";
            auto& items = std::get<schema_ptr>(*s.items);
            formatter.plural = true;
            add_inner_entry(*items);
            formatter.plural = false;
        } else if (s.prefixItems.has_value()) {
            data << format_first_word("Tuple") << " (json array) with these " << s.prefixItems->size() << " entries: <ol>";
            for (auto& e : *s.prefixItems) {
                data << "<li>";
                add_inner_entry(e);
                data << "</li>";
            }
            data << "</ol>";
        } else {
            return false;
        }
        return true;
    }
    void add_range(const glz::schema& s)
    {
        auto formatNumber {
            [](const auto& variant) {
                return std::visit([](const auto& number) {
                    return std::format("{}", number);
                },
                    variant);
            }
        };
        if (s.minimum) {
            if (s.maximum) {
                data << " in [" << formatNumber(*s.minimum) << "," << formatNumber(*s.maximum) << "]";
            } else {
                data << ">=" << formatNumber(*s.minimum);
            }

        } else if (s.maximum) {
            data << "<=" << formatNumber(*s.maximum);
        }
    }
    bool try_pods(std::string_view type, const glz::schema& s)
    {
        if (type == "string") {
            data << format_first_word("String");
        } else if (type == "boolean") {
            data << format_first_word("Boolean");
        } else if (type == "number") {
            data << format_first_word("Number");
            add_range(s);
        } else if (type == "integer") {
            data << format_first_word("Integer");
            add_range(s);
        } else {
            return false;
        }
        return true;
    }
    // dispatches to the above
    void add_inner_entry(const glz::schema& s)
    {
        bool handled = false;
        if (s.oneOf.has_value()) {
            data << format_first_word("Variant") << ", one of the following types:\n<ul>";
            for (auto& s : *s.oneOf) {
                data << "<li>";
                add_inner_entry(s);
                data << "</li>";
            }
            data << "</ul>";
            handled = true;
        } else if (s.anyOf.has_value()) {
            auto& v { *s.anyOf };
            auto has_type { [](glz::schema s, std::string_view t) {
                if (!s.type.has_value())
                    return false;
                if (auto p { std::get_if<std::string_view>(&*s.type) }) {
                    return *p == t;
                }
                return false;
            } };
            if (v.size() == 2) {
                const glz::schema* p { nullptr };
                if (has_type(v[0], "null")) {
                    p = &v[1];
                } else if (has_type(v[1], "null")) {
                    p = &v[0];
                }
                if (p) {
                    data << "either null or ";
                    add_inner_entry(*p);
                    handled = true;
                }
            }
        } else if (s.ref) {
            data << gen_link(normalize_name(s.ref));
            handled = true;
        } else if (s.type) {
            if (std::holds_alternative<std::string_view>(*s.type)) {
                const auto& type = std::get<std::string_view>(*s.type);
                if (type == "object") {
                    handled = add_object(s);
                } else if (type == "array") {
                    handled = add_array(s);
                } else {
                    handled = try_pods(type, s);
                }
            } else if (std::holds_alternative<std::vector<std::string_view>>(*s.type)) {
                auto& v { std::get<std::vector<std::string_view>>(*s.type) };
                if (v.size() == 2) {
                    std::optional<std::string_view> opt_type;
                    if (v[0] == "null") {
                        opt_type = v[1];
                    } else if (v[1] == "null") {
                        opt_type = v[0];
                    }
                    if (opt_type) {
                        handled = try_pods(*opt_type, s);
                    }
                }
            }
        }
        if (!handled) {
            data << std::format("<pre>{}</pre>", html_escape(glz::write<options>(s).value()));
        }
    }
    void add_entry(const std::string_view& name, const glz::schema& s)
    {
        auto hl = html_escape(name);
        data << "<section id=\"" + hl + "\"><h2>" + hl + "</h2>\n";
        add_inner_entry(s);
        data << "</section>\n";
    }
    std::string finalize() &&
    {
        data << R"(
     </ul> 
     </body> 
</html>)";
        return std::move(data).str();
    }
};
HTMLString SchemaAggregator::to_html_list() const
{
    HTMLAggregator ha;
    for (auto& [name, schematic] : defs)
        ha.add_entry(name, schematic);
    return std::move(ha).finalize();
}

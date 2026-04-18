#include "schema_aggregator.hpp"
#include "../html_escape.hpp"
#include <sstream>

struct HTMLAggregator {
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
            data << "<p>Objct with properties:</p>";
            data << " <table>";
            for (auto [key, val] : s.properties.value()) {
                data << "<tr><td>" << key << "</td><td>";
                add_inner_entry(val);
                data << "</td></tr>";
            }
            data << "</table>";
        } else if (s.additionalProperties && std::holds_alternative<schema_ptr>(*s.additionalProperties)) {
            auto& item = std::get<schema_ptr>(*s.additionalProperties);
            data << "<p>Object with values of type";
            add_inner_entry(*item);
        } else {
            return false;
        }
        return true;
    }
    bool add_array(const glz::schema& s)
    {
        if (s.items.has_value() && std::holds_alternative<schema_ptr>(*s.items)) {
            data << "<p>Array with items of type:</p>";
            auto& items = std::get<schema_ptr>(*s.items);
            add_inner_entry(*items);
        } else if (s.prefixItems.has_value()) {
            data << "<p>Array with exactly these items:</p>";
            for (auto& e : *s.prefixItems) {
                add_inner_entry(e);
            }
        } else {
            return false;
        }
        return true;
    }
    void add_string(const glz::schema&)
    {
        data << "string";
    }
    // dispatches to the above
    void add_inner_entry(const glz::schema& s)
    {
        bool handled = false;
        if (s.ref) {
            data << gen_link(normalize_name(s.ref));
            handled = true;
        } else if (s.type && std::holds_alternative<std::string_view>(*s.type)) {
            const auto& type = std::get<std::string_view>(*s.type);
            if (type == "object") {
                handled = add_object(s);
            } else if (type == "array") {
                handled = add_array(s);
            } else if (type == "string") {
                add_string(s);
                handled = true;
            } else if (type == "boolean") {
                data << "boolean";
                handled = true;
            }
        }
        if (!handled) {
            data << std::format("<code><pre>{}</pre></code>", html_escape(glz::write<options>(s).value()));
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

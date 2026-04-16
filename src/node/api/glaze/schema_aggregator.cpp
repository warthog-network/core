#include "schema_aggregator.hpp"
#include "../html_escape.hpp"

HTMLString SchemaAggregator::to_html_list() const
{
    auto normalize_name { [](std::string_view str) {
        size_t pos = str.find_last_of('/');

        if (pos == std::string::npos) {
            return html_escape(str);
        }
        return html_escape(str.substr(pos + 1));
    } };
    constexpr glz::opts Opts;
    using namespace std::string_literals;
    static constexpr glz::opts options = glz::opts_write_type_info_off<decltype(Opts)> { { Opts } };

    std::string out = R"(
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
    out += "<ul>\n";
    for (auto& [name, schematic] : defs) {
        auto hl = html_escape(name);
        auto val = [&] {
            auto gen_link { [](std::string_view name) { return std::format("<a href=\"#{}\">{}</a>", name, name); } };
            using schema_ptr = std::shared_ptr<glz::schema>;
            if (schematic.type && std::holds_alternative<std::string_view>(*schematic.type) && std::get<std::string_view>(*schematic.type) == std::string_view("object")) {
                if (schematic.properties) {
                    std::string table { " <table>" };
                    for (auto [key, val] : schematic.properties.value()) {
                        if (val.ref) {
                            auto n { normalize_name(*val.ref) };
                            table += std::format("<tr><td>{}</td><td>{}</td></tr>", key, gen_link(n));
                        } else if (val.type.has_value() && std::holds_alternative<std::string_view>(val.type.value())) {
                            auto n { std::get<std::string_view>(val.type.value()) };
                            table += std::format("<tr><td>{}</td><td>{}</td></tr>", key, n);
                        } else {
                            table += std::format("<tr><td>{}</td><td>{}</td></tr>", key, html_escape(glz::write<options>(schematic).value()));
                        }
                    }
                    table += "</table>";
                    return table;
                } else if (schematic.additionalProperties && std::holds_alternative<schema_ptr>(*schematic.additionalProperties)) {
                    auto& item = std::get<schema_ptr>(*schematic.additionalProperties);
                    if (item->ref) {
                        return std::format("<p>Object with values of type {}</p>", gen_link(normalize_name(item->ref.value())));
                    }
                }
            } else if (schematic.items && std::holds_alternative<schema_ptr>(*schematic.items)) {
                auto& items = std::get<schema_ptr>(*schematic.items);
                if (items->ref) {
                    return std::format("<p>Array with items of type {}</p>", gen_link(normalize_name(items->ref.value())));
                }
            }
            return std::format("<code><pre>{}</pre></code>", html_escape(glz::write<options>(schematic).value()));
        }();
        out += std::format("<section id=\"{}\"><h2>{}</h2>\n{}</section>\n", hl, hl, val);
    }
    out += "</ul> </body> </html>";
    return out;
}

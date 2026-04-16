#pragma once
#include "glaze/json/schema.hpp"
#include "api/reply.hpp"
#include "api/reply.hpp"
#include <map>
#include <string>
struct SchemaAggregator {
private:
    std::map<std::string_view, glz::schema, std::less<>> defs;

public:
    template <typename T>
    std::string_view add_type()
    {
        auto name { glz::name_v<T> };
        auto& def = defs[name];
        if (!def.type) {
            glz::detail::to_json_schema<std::decay_t<T>>::template op<glz::opts {}>(def, defs);
        }
        return name;
    }
    HTMLString to_html_list() const;
    JSONString to_string() const
    {
        constexpr glz::opts Opts;
        static constexpr glz::opts options = glz::opts_write_type_info_off<decltype(Opts)> { { Opts } };
        std::string buf;
        auto res { glz::write<options>(defs, buf) };
        assert(!res);
        return buf;
    }
};

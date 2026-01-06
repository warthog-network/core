#include "with_uint64.hpp"
#include "nlohmann/json.hpp"
#include "reader.hpp"

IsUint32::IsUint32(Reader& r)
    : IsValue(r.uint32()) { };

IsUint64::IsUint64(Reader& r)
    : IsValue(r.uint64()) { };

IsUint32::operator nlohmann::json() const
{
    return nlohmann::json(val);
};

std::string IsUint64::to_string() const
{
    return std::to_string(val);
}
IsUint64::operator nlohmann::json() const
{
    return nlohmann::json(val);
};

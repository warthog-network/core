#include "serializer.hxx"
#include "hex.hpp"

template <typename T>
requires RawSerializing<, T>
std::string to_hex(T&& t)
{
}

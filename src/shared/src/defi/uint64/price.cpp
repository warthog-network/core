
#include "price.hpp"
#include "general/reader.hpp"
Price_uint64::Price_uint64(Reader& r)
    : _m(r)
    , _e(r)
{
}

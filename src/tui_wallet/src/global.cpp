#include "global.hpp"

namespace {
struct Services {
    Services()
    {
        ECC_Start();
    }
    ~Services()
    {
        ECC_Stop();
    }
} services;
}

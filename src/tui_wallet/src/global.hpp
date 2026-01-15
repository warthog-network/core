#pragma once
#include "data/interface.hpp"
#include <memory>
namespace global {
struct Globals {
    DataInterface dataInterface;
    Globals(DataRetrievalContext init)
        : dataInterface(std::move(init))
    {
    }
};

inline std::unique_ptr<Globals> ptr;
inline void init(DataRetrievalContext g)
{
    ptr = std::make_unique<Globals>(std::move(g));
}
inline auto& globals() { return *ptr; }
inline auto& data_interface() { return ptr->dataInterface; }
inline auto& endpoint() { return data_interface().retrievalContext.endpoint; }
}


#pragma once
#include "data/interface.hpp"
#include "log_channel.hpp"
#include "tui/gui.hpp"
// #include "log_lines.hpp"
#include <memory>
namespace global {
struct Globals {
    DataInterface dataInterface;
    // LogLines log;
    LogChannel logChannel;
    RequestLogChannel requestLogChannel;
    Globals(ui::GUI& gui, DataRetrievalContext init)
        : dataInterface(std::move(init),
              [&gui](std::function<void()> f) {
                    gui.defer([f=std::move(f)](){ 

                        f();}); })
    {
    }
};

inline std::unique_ptr<Globals> ptr;

inline void init(ui::GUI& gui, DataRetrievalContext g)
{
    ptr = std::make_unique<Globals>(gui, std::move(g));
}
inline auto& globals() { return *ptr; }
inline auto& data_interface() { return ptr->dataInterface; }
inline auto& wallet() { return data_interface().ctx().retrievalContext.wallet; }
inline auto& endpoint() { return data_interface().retrieval_context().endpoint; }
inline auto& log() { return globals().logChannel; }
inline auto& request_log() { return globals().requestLogChannel; }
}

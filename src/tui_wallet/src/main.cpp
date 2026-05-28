#include "api/endpoint.hpp"
#include "api/parse.hpp"
#include "communication/create_transaction.hpp"
#include "global.hpp"
#include "spdlog/spdlog.h"
#include "tui/gui.hpp"
#include "tui/tabs.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

std::vector<std::string> get_files(std::string path = ".")
{
    std::vector<std::string> out;
    for (const auto& entry : fs::directory_iterator(path)) {
        out.push_back(entry.path().filename().string());
    }
    return out;
}

[[nodiscard]] std::optional<std::string> read_file(const std::string filename)
{
    FILE* fp = fopen(filename.c_str(), "r");
    if (!fp) {
        return {};
    }

    std::string v;
    char buf[1024];
    while (size_t len = fread(buf, 1, sizeof(buf), fp))
        v.insert(v.end(), buf, buf + len);
    fclose(fp);
    return v;
}

Wallet load_wallet(const std::string filename = "tui_wallet.json")
{
    auto content { read_file(filename) };
    if (!content) {
        Wallet w;
        w.save(filename);
        return w;
    } else {
        try {
            return Wallet(*content);
        } catch (std::exception& e) {
            spdlog::error(e.what());
            auto errMsg {
                std::format("Cannot load wallet file '{}.'", filename)
            };
            throw std::runtime_error(errMsg);
        }
    }
}

void init_globals(ui::GUI& gui)
{
    global::init(gui, { .endpoint { "localhost", 3100 }, .wallet { load_wallet() } });
}

struct AccountBalance {
    struct {
        int id;
    } token;
};

int main()
{
    try {
        auto gui { ui::GUI::create_instance() };
        init_globals(*gui);
        bool shutdown = false;
        std::condition_variable cv;
        std::mutex m;
        std::thread t([&]() {
            bool b { false };
            while (true) {
                {
                    std::unique_lock l(m);
                    if (cv.wait_for(l, std::chrono::seconds(1),
                            [&]() { return shutdown == true; }))
                        break;
                }
                b = !b;
                gui->set_connected(b);
                gui->set_unlocked(!b);
            }
            gui->terminate();
        });
        gui->run();
        {
            std::unique_lock l(m);
            shutdown = true;
        }
        cv.notify_one();
        t.join();
        shutdownState.apply([&](const std::optional<std::string>& e) {
            if (e.has_value()) {
                spdlog::error(e.value());
                abort();
            }
        });
    } catch (const std::exception& e) {
        spdlog::error(e.what());
        abort();
    }
    return 0;
}

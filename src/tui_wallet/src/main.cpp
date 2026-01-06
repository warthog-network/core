#include "api/api_call.hpp"
#include "global.hpp"
#include "tui/gui.hpp"
#include <filesystem>
#include <iostream>
#include <string>

using std::cout;
using std::endl;
namespace fs = std::filesystem;
std::vector<std::string> get_files(std::string path = ".")
{
    std::vector<std::string> out;
    for (const auto& entry : fs::directory_iterator(path)) {
        out.push_back(entry.path().filename().string());
    }
    return out;
}

void init_globals()
{
    // TODO don't hardcode wallet
    global::globals = { .walletptr = std::make_unique<Wallet>(PrivKey("02e8005492d1edb977c0387af96687d569dcbe7171b4740dc4f45291a830e594")) };
}

// int main()
// {
//     std::string path = ".";
//     cout << global::wallet().to_string() << endl;
//
//     // try {
//     //     for (const auto& entry : fs::directory_iterator(path)) {
//     //         std::cout << entry.path().filename().string() << std::endl;
//     //     }
//     // } catch (const fs::filesystem_error& ex) {
//     //     std::cerr << "Error: " << ex.what() << std::endl;
//     // }
// }

int main()
{

    using namespace std;
    Endpoint e("localhost", 3100);
    cout << e.get_balance("1", TokenId::WART).to_string() << endl;
    return 0;
    ECC_Start();
    init_globals();
    auto gui { ui::GUI::create_instance() };
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

    ECC_Stop();
    return 0;
}

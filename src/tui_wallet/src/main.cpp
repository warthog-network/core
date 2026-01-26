#include "api/endpoint.hpp"
#include "api/parse.hpp"
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

void init_globals(ui::GUI& gui)
{
    global::init(gui, { .endpoint { "localhost", 3100 },
                          // TODO don't hardcode wallet
                          .wallet { PrivKey("02e8005492d1edb977c0387af96687d569dcbe7171b4740dc4f45291a830e594") } });
}

const auto jsonStr = R"({
 "code": 0,
 "data": {
  "account": {
   "accountId": 9,
   "address": "2de77d5e23dc63e4c4149d394c979361e9e8e966336c8cd4"
  },
  "balance": {
   "locked": {
    "precision": 8,
    "str": "0",
    "u64": 0
   },
   "total": {
    "precision": 8,
    "str": "3.00000000",
    "u64": 300000000
   }
  },
  "lookupTrace": {
   "fails": [],
   "snapshotHeight": null
  },
  "token": {
   "id": 1,
   "name": "WART",
   "precision": 8,
   "spec": "asset:0000000000000000000000000000000000000000000000000000000000000000"
  }
 }
})";
struct AccountBalance {
    struct {
        int id;
    } token;
};

int main()
{
    // using namespace global;
    // cout << wallet().address() << endl;
    // cout << wallet().get_wart_balance().total.to_string() << endl;
    // return 0;

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

    return 0;
}

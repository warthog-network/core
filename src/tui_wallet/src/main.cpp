#include "api/endpoint.hpp"
#include "api/parse.hpp"
#include "communication/create_transaction.hpp"
#include "global.hpp"
#include "tui/gui.hpp"
#include "tui/tabs.hpp"
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
    // cout << wallet().get_wart_balance().total.to_string() << endl;
    // return 0;

    auto gui { ui::GUI::create_instance() };
    init_globals(*gui);

    // auto &r{global::globals().dataInterface.retrieval_context()};
    // auto& e { global::globals().dataInterface.retrieval_context().endpoint };
    // auto [pinHeight, pinHash] { e.get_pin() };
    // cout << "pinHeight: " << pinHeight.value() << " pinHash: " << serialize_hex(pinHash) << endl;

    // TransactionCreateBase(PinHeight pinHeight, NonceId nonceId, CompactUInt compactFee, Ts... ts, const Hash& pinHash, const PrivKey& pk, NonceReserved reserved = NonceReserved::zero())
    // try {
    //     cout << "Own address: " << global::wallet().address.to_string() << endl;
    //     Address toAddr("3661579d61abde5837a8686dc4d65348a2fc61b1fe5f4093");
    //     NonzeroWart wart { 1 };
    //     TransactionCreateContext ctx= global::wallet(). {
    //         .pinHash { pinHash },
    //         .pinHeight { pinHeight },
    //         .nonceId { 0 },
    //         .compactFee { CompactUInt::compact(Wart(0)) },
    //         .pk { global::wallet().privKey }
    //     };
    //     WartTransferCreate c1(ctx, toAddr, wart);
    //
    //     AssetHash ah { AssetHash::parse_throw("0e4825efffa294610d2ac376713e3bcc9b53d378e823834b64e5df01f75d3b0c") };
    //     Funds_uint64 amount(1);
    //     Funds_uint64 shares(1);
    //     TokenTransferCreate c2(ctx, ah, false, toAddr, amount);
    //     LimitSwapCreate c3(ctx, ah, true, amount, Price_uint64::from_double(1).value());
    //     LiquidityDepositCreate c4(ctx, ah, amount, wart);
    //     PinHeight cancelHeight(pinHeight);
    //     LiquidityWithdrawalCreate c5(ctx, ah, amount);
    //     NonceId cancelNonce(1);
    //     CancelationCreate c6(ctx, cancelHeight, cancelNonce);
    //     FundsDecimal assetSupply(Funds_uint64(1000000000), 3);
    //     AssetName assetName { "ASSET" };
    //     AssetCreationCreate c7(ctx, assetSupply, assetName);
    //     // DEFINE_CREATE_MESSAGE(AssetCreationCreate, ::block::labels::assetCreation, AssetSupplyEl, AssetNameEl)
    //     auto& c{c7};
    //     std::string json { c };
    //     cout << json << endl;
    //     cout << "Sent transaction: " << serialize_hex(e.send_transaction(c)) << endl;
    //     ;
    // } catch (std::runtime_error& e) {
    //     cout << "Error: " << e.what() << endl;
    // }
    // return 0;
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

#include "api_call.hpp"
#include "cmdline/cmdline.h"
#include "communication/create_transaction.hpp"
#include "crypto/crypto.hpp"
#include "general/hex.hpp"
#include "general/params.hpp"
#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;
using namespace nlohmann;

struct Wallet {
    PrivKey privKey;
    PubKey pubKey;
    Address address;
    std::string to_string() const
    {
        nlohmann::json j;
        j["privateKey"] = privKey.to_string();
        j["publicKey"] = pubKey.to_string();
        j["address"] = address.to_string();
        return j.dump(1);
    }
    void save(const filesystem::path& path) const
    {
        if (std::filesystem::exists(path)) {
            throw std::runtime_error("Cannot create wallet, file '" + path.string() + "' already exists. You can specify a filename using the '-f' option.");
        }
        ofstream os(path);
        if (os.good()) {
            os << to_string();
            if (os.good())
                cout << "Wallet file created." << endl;
            else
                throw std::runtime_error("Could not write wallet file, file is now corrupted :)");
        } else {
            throw std::runtime_error("Cannot create wallet file.");
        }
    }

private:
    Wallet(json parsed)
        : Wallet(PrivKey(parsed["privateKey"].get<std::string>()))
    {
        std::string pubKeyString = parsed["publicKey"].get<std::string>();
        std::string addressString = parsed["address"].get<std::string>();
        if ((PubKey(pubKeyString) != pubKey) || (Address(addressString) != address)) {
            throw std::runtime_error("Inconsistent data.");
        }
    }

public:
    Wallet(PrivKey k = PrivKey())
        : privKey(k)
        , pubKey(privKey.pubkey())
        , address(pubKey.address())
    {
    }
    Wallet(const std::string& jsonstr)
        : Wallet(json::parse(jsonstr))
    {
    }
};

Wallet open_wallet(const filesystem::path& path)
{
    ifstream file(path);
    ostringstream ss;
    if (file.is_open()) {
        ss << file.rdbuf();
        try {
            Wallet w(ss.str());
            return w;
        } catch (std::runtime_error& e) {
            throw std::runtime_error(
                "Wallet file corrupted: "s + e.what() + ". You might want to restore using a private key."s);
        } catch (nlohmann::detail::parse_error& e) {
            throw std::runtime_error(
                "Wallet file corrupted: File has incorrect JSON structure. You might "
                "want to restore using a private key.");
        }
    } else {
        if (std::filesystem::exists(path)) {
            throw std::runtime_error("Cannot read file '" + path.string() + "'.");
        } else {
            throw std::runtime_error("Wallet file '" + path.string() + "' does not exist yet. Create a new wallet file "
                                                                       "using the '-c' flag.");
        }
    }
}
Funds_uint64 parse_amount(std::string amount)
{
    auto parsed { Funds_uint64::parse(amount) };
    if (!parsed) {
        throw std::runtime_error("Cannot parse amount \"" + amount + "\"");
    }
    return *parsed;
}
std::string read_with_msg(std::string msg)
{
    cout << msg;
    std::string input;
    cin >> input;
    return input;
}

Funds_uint64 read_fee(std::string msg)
{
    return parse_amount(read_with_msg(msg));
}

Funds_uint64 read_amount(auto balance_lambda, CompactUInt cfee)
{
    auto input { read_with_msg("Amount (type \"max\" for total balance): ") };
    if (input == "max") {
        Funds_uint64 balance { balance_lambda() };
        auto fee(cfee.uncompact());
        if (balance <= fee)
            throw std::runtime_error("Insufficient funds");
        return diff_assert(balance, fee);
    }
    return parse_amount(input);
}

Address read_address(std::string msg)
{
    return Address(read_with_msg(msg));
}

int process(gengetopt_args_info& ai)
{
    bool action = false;
    size_t sum_actions = ai.address_given + ai.balance_given + ai.send_given;
    if (sum_actions > 1) {
        cerr << "Invalid combination of --address, --balance and --send.";
        return -1;
    }

    Endpoint endpoint(ai.host_arg, ai.port_arg);
    try {
        wrt::optional<Wallet> w;
        if (ai.create_given) {
            if (ai.restore_given) {
                cerr << "Invalid option combination (--create and --restore)." << endl;
                return -1;
            }
            w = Wallet {};
        }
        if (ai.restore_given)
            w = Wallet { PrivKey(ai.restore_arg) };

        filesystem::path walletpath(ai.file_arg);
        if (w) {
            action = true;
            if (!ai.print_only_given)
                w->save(walletpath);
        } else // load from file
            w = open_wallet(walletpath);

        if (sum_actions == 0) {
            cout << w->to_string() << endl;
            return 0;
        }

        if (ai.address_given) {
            cout << w->address.to_string() << endl;
            return 0;
        }
        auto balance_lambda = [&]() {
            return endpoint.get_balance(w->address.to_string());
        };
        if (ai.balance_given) {
            cout << balance_lambda().to_string() << endl;
            return 0;
        }
        if (ai.send_given) {
            bool interactive { !ai.to_given || !ai.fee_given || !ai.amount_given };
            Address to(ai.to_given ? Address(ai.to_arg) : read_address("To: "));
            CompactUInt fee { CompactUInt::compact( ai.fee_given ? parse_amount(ai.fee_arg) : read_fee("Fee: ") ) };
            Funds_uint64 amount { ai.amount_given ? parse_amount(ai.amount_arg) : read_amount(balance_lambda, fee) };
            NonceId nid { ai.nonce_given ? NonceId(ai.nonce_arg) : NonceId::random() };
            if (interactive) {
                cout << "Summary:"
                     << "\n  To:     " << to.to_string()
                     << "\n  Fee:    " << fee.uncompact().to_string()
                     << "\n  Amount: " << amount.to_string()
                     << "\n  Nonce: " << nid.value()
                     << "\nConfirm with \"y\": ";
                std::string input;
                cin >> input;
                if (input != "y" && input != "Y")
                    throw std::runtime_error("Not confirmed.");
            }
            cout << "Get pin" << endl;
            auto pin = endpoint.get_pin();
            cout << "Got pin" << endl;
            WartTransferCreate m(pin.first, pin.second, w->privKey, fee, to, amount, nid);
            assert(m.valid_signature(pin.second, w->address));
            cout << "NonceId: " << m.nonceId.value() << endl;
            cout << "pinHeight: " << m.pinHeight.value() << endl;
            cout << "pinHeight: " << pin.first.value() << endl;
            cout << "pinHash: " << serialize_hex(pin.second) << endl;
            std::string msg;

            auto e = endpoint.send_transaction(m);

            if (std::holds_alternative<Endpoint::Error>(e)) {
                auto& err { std::get<Endpoint::Error>(e) };
                cout << "Transaction rejected (code " << err.code << "): " << err.message;
                return -1;
            } else {
                cout << "Transaction accepted.\n"
                     << "Hash: " << serialize_hex(std::get<TxHash>(e)) << endl;
                return 0;
            }
        }
    } catch (std::exception& e) {
        cerr << e.what() << endl;
        return -1;
    }
    if (!action) {
        cerr << "Please specify some option. Print help using the '-h' flag."
             << endl;
        return -1;
    }
    return 0;
}
int main(int argc, char** argv)
{
    ECC_Start();
    gengetopt_args_info ai;
    if (cmdline_parser(argc, argv, &ai) != 0) {
        return -1;
    }
    int i = process(ai);
    cmdline_parser_free(&ai);
    ECC_Stop();
    return i;
}

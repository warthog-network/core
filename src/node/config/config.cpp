#include "config.hpp"
#include "browser.hpp"
#include "cmdline/cmdline.hpp"
#include "general/errors.hpp"
#include "general/is_testnet.hpp"
#include "spdlog/spdlog.h"
#include "toml++/toml.hpp"
#include "transport/helpers/tcp_sockaddr.hpp"
#include "version_def.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#ifdef __APPLE__
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <pwd.h>
#endif

using namespace std;

#ifndef DISABLE_LIBUV
std::string ConfigParams::get_default_datadir()
{
    const char* osBaseDir = nullptr;
#ifdef __linux__
    if ((osBaseDir = getenv("HOME")) == NULL) {
        osBaseDir = getpwuid(getuid())->pw_dir;
    }
    if (osBaseDir == nullptr)
        throw std::runtime_error("Cannot determine default data directory.");
    return std::string(osBaseDir) + "/.warthog/";
#elif _WIN32
    osBaseDir = getenv("LOCALAPPDATA");
    if (osBaseDir == nullptr)
        throw std::runtime_error("Cannot determine default data directory.");
    return std::string(osBaseDir) + "/Warthog/";
#elif __APPLE__
    if ((osBaseDir = getenv("HOME")) == NULL) {
        osBaseDir = getpwuid(getuid())->pw_dir;
    }
    if (osBaseDir == nullptr)
        throw std::runtime_error("Cannot determine default data directory.");
    return std::string(osBaseDir) + "/Library/Warthog/";
#else
    throw std::runtime_error("Cannot determine default data directory.");
#endif
}

#else

#include <emscripten.h>
#include <emscripten/wasmfs.h>
#include <unistd.h>
bool ConfigParams::mount_opfs(const char* mountpoint)
{
    spdlog::info("Mounting OPFS at {}", mountpoint);
    auto pOpfs = wasmfs_create_opfs_backend();
    if (!pOpfs)
        return false;
    bool exists = access(mountpoint, F_OK) == 0;
    if (exists)
        return true;

    const int rc = wasmfs_create_directory(mountpoint, 0777, pOpfs);
    return rc == 0;
}
std::string ConfigParams::get_default_datadir()
{
    return "/opfs/";
}
#endif

namespace {
// wrt::optional<SnapshotSigner> parse_leader_key(std::string privKey)
// {
//     try {
//         SnapshotSigner ss { PrivKey(privKey) };
//         spdlog::warn("This node signs chain snapshots with priority {}", ss.get_importance());
//         return ss;
//     } catch (Error e) {
//         spdlog::warn("Cannot parse leader key, ignoring.");
//     }
//     return {};
// }

struct CmdlineParsed {
    static wrt::optional<CmdlineParsed> parse(int argc, char** argv)
    {
        gengetopt_args_info ai;
        if (cmdline_parser(argc, argv, &ai) != 0)
            return {};
        return CmdlineParsed { ai };
    }
    CmdlineParsed(const CmdlineParsed&) = delete;
    CmdlineParsed(CmdlineParsed&& other)
        : ai(other.ai)
    {
        other.deleteOnDestruction = false;
    };
    ~CmdlineParsed()
    {
        if (deleteOnDestruction) {
            cmdline_parser_free(&ai);
        }
    }
    auto& value() const { return ai; }

private:
    CmdlineParsed(gengetopt_args_info& ai0)
        : ai(ai0)
    {
    }

    bool deleteOnDestruction { true };
    gengetopt_args_info ai;
};
}

wrt::expected<ConfigParams, int> ConfigParams::from_args(int argc, char** argv)
{
    auto p { CmdlineParsed::parse(argc, argv) };
    if (!p)
        return tl::make_unexpected(-1);

    ConfigParams c;
    if (auto i { c.init(p->value()) }; i < 1) {
        return tl::make_unexpected(i);
    }
    return wrt::expected<ConfigParams, int>(c);
}

#ifndef DISABLE_LIBUV
namespace {
Endpoints parse_endpoints(std::string csv)
{
    std::vector<TcpPin> out;
#ifndef DISABLE_LIBUV
    std::string::size_type pos = 0;
    while (true) {
        auto end = csv.find(",", pos);
        auto param = csv.substr(pos, end - pos);
        auto parsed = TcpPin::parse(param);
        if (!parsed) {
            throw std::runtime_error("Invalid parameter '"s + param + "'."s);
        }
        out.push_back(parsed.value());
        if (end == std::string::npos) {
            break;
        }
        pos = end + 1;
    }
#endif
    return out;
}
template <typename T, typename flag_t>
struct ConfigFiller;
std::runtime_error failed_convert(const toml::node& n)
{
    return std::runtime_error("Cannot parse configuration value starting at line "s + std::to_string(n.source().begin.line) + ", column "s + std::to_string(n.source().begin.column) + ".");
}

template <typename T>
wrt::optional<T> config_convert(const toml::node& n)
{
    if (auto val = n.value<T>()) {
        return val.value();
    }
    throw failed_convert(n);
}

template <>
wrt::optional<TcpPin> config_convert(const toml::node& n)
{
    if (auto sv { n.value<std::string_view>() }) {
        if (sv->length() == 0)
            return {};
        if (auto a { TcpPin::parse(*sv) })
            return *a;
    }
    throw failed_convert(n);
}
template <>
wrt::optional<TCPPeeraddr> config_convert(const toml::node& n)
{
    if (auto sv { n.value<std::string_view>() }) {
        if (sv->length() == 0)
            return {};
        if (auto a { TCPPeeraddr::parse(*sv) })
            return *a;
    }
    throw failed_convert(n);
}

template <>
wrt::optional<Endpoints> config_convert(const toml::node& n)
{
    if (n.is_array()) {
        Endpoints endpoints;
        auto& a { *n.as_array() };
        for (auto& e : a) {
            auto a { config_convert<TcpPin>(e) };
            if (!a)
                goto failed;
            endpoints.push_back(*a);
        }
        return endpoints;
    }
failed:
    throw failed_convert(n);
}

template <>
wrt::optional<SnapshotSigner>
config_convert(const toml::node& n)
{
    try {
        if (auto sv { n.value<std::string_view>() })
            return SnapshotSigner { PrivKey(*sv) };
    } catch (std::exception& e) {
        spdlog::error(e.what());
    }
    throw failed_convert(n);
}

struct TableReaderData {
    const toml::table& tbl;
    std::string_view filepath;
    mutable std::map<toml::key, bool> keyUsed;
};

struct TableReader : public TableReaderData {
    bool report { true };
    TableReader(const toml::table& tbl, std::string_view filepath)
        : TableReaderData(tbl, filepath, {})
    {
        for (auto& [k, v] : tbl) {
            keyUsed.emplace(k, false);
        }
    }
    TableReader(const TableReader&) = delete;
    TableReader(TableReader&& a)
        : TableReaderData(std::move(a))
    {
        a.report = false;
    };
    ~TableReader()
    {
        if (report) {
            for (auto& [k, used] : keyUsed) {
                if (!used) {
                    spdlog::warn("Ignoring configuration setting \""s + std::string(k.str()) + "\" at line "s + std::to_string(k.source().begin.line) + " in file "s + string(filepath));
                }
            }
        }
    }

    wrt::optional<TableReader> subtable(std::string_view s)
    {

        if (auto it { tbl.find(s) }; it != tbl.end()) {
            keyUsed[it->first] = true;
            if (it->second.is_table() == false)
                throw std::runtime_error("Configuration file's "s + std::string(s) + " must be a table."s);
            auto p { it->second.as_table() };
            assert(p != nullptr);
            return TableReader { *p, filepath };
        }
        return wrt::nullopt;
    }

    struct Entry {
        const toml::node* v;

        template <typename T>
        wrt::optional<T> get() const
        {
            return config_convert<T>(*v);
        }
    };
    wrt::optional<Entry> operator[](std::string_view key) const
    {
        if (auto it { tbl.find(key) }; it != tbl.end()) {
            keyUsed[it->first] = true;
            return { Entry { &it->second } };
        }
        return wrt::nullopt;
    }
};

template <typename U>
void fill_arg(
    auto& dst,
    bool flag_given,
    U& flag_val,
    auto flag_map)
{
    if (flag_given)
        dst = flag_map(flag_val);
}

template <typename U>
void fill_arg(
    auto& dst,
    bool flag_given,
    U& flag_val)
{
    fill_arg<U>(dst, flag_given, flag_val,
        [](U& u) { return u; });
}

// fill_arg(peers.connect, ai.connect_given, ai.connect_arg, parse_endpoints);
template <typename T>
void fill(
    T& dst,
    wrt::optional<TableReader>& tblreader,
    std::string_view tblkey)
{
    if (tblreader) {
        if (auto oe { (*tblreader)[tblkey] }) {
            if (auto v { oe->get<T>() }) {
                dst = *v;
                return;
            }
        }
    }
}

template <typename T>
void fill(
    wrt::optional<T>& dst,
    wrt::optional<TableReader>& tblreader,
    std::string_view tblkey)
{
    if (tblreader) {
        if (auto oe { (*tblreader)[tblkey] }) {
            if (auto v { oe->get<T>() }) {
                dst = *v;
                return;
            }
        }
    }
}

//
// template <typename T, typename U>
// [[nodiscard]] T fill_default(
//     wrt::optional<TableReader>& tblreader,
//     std::string_view tblkey,
//     auto default_val,
//     bool flag_given,
//     U& flag_val,
//     auto flag_map)
// {
//     auto o { fill_optional<T>(tblreader, tblkey, flag_given, flag_val, flag_map) };
//     if (!o)
//         return default_val;
//     return *o;
// }
//
// template <typename T, typename U>
// [[nodiscard]] T fill_default(
//     wrt::optional<TableReader>& tblreader,
//     std::string_view tblkey,
//     auto default_val,
//     bool flag_given,
//     U& flag_val)
// {
//     return fill_default<T, U>(tblreader, tblkey, std::move(default_val), flag_given, flag_val,
//         [](U& u) { return u; });
// }
//
// template <typename T>
// [[nodiscard]] T fill_default(
//     wrt::optional<TableReader>& tblreader,
//     std::string_view tblkey,
//     auto default_val)
// {
//     auto o { fill_optional<T>(tblreader, tblkey) };
//     if (!o)
//         return default_val;
//     return *o;
// }

} // namespace

void ConfigParams::process_args(const gengetopt_args_info& ai)
{
    auto arg_to_tcppeeraddr_lambda = [](string_view argname) {
        return [argname](std::string_view argval) {
            auto p = TCPPeeraddr::parse(argval);
            if (!p)
                throw std::runtime_error("Bad "s + string(argname) + " option specified.");
            return *p;
        };
    };
    auto arg_to_minfee { [](std::string_view argval) {
        try {
            return CompactUInt::compact(Wart::parse_throw(argval), true);
        } catch (...) {
            throw std::runtime_error("Bad --minfee option '"s + std::string(argval) + "' specified");
        }
    } };
    fill_arg(peers.connect, ai.connect_given, ai.connect_arg, parse_endpoints);
    fill_arg(node.bind, ai.bind_given, ai.bind_arg, arg_to_tcppeeraddr_lambda("--bind"));
    fill_arg(node.minMempoolFee, ai.minfee_given, ai.minfee_arg, arg_to_minfee);
    fill_arg(jsonrpc.bind, ai.rpc_given, ai.rpc_arg, arg_to_tcppeeraddr_lambda("--rpc"));
    fill_arg(publicAPI, ai.publicrpc_given, ai.publicrpc_arg, arg_to_tcppeeraddr_lambda("--publicrpc"));
    fill_arg(stratumPool, ai.stratum_given, ai.stratum_arg, arg_to_tcppeeraddr_lambda("--stratum"));
    fill_arg(data.chaindb, ai.chain_db_given, ai.chain_db_arg);
    fill_arg(data.peersdb, ai.peers_db_given, ai.peers_db_arg);
    fill_arg(data.rxtxdb, ai.rxtx_db_given, ai.rxtx_db_arg);
    node.isolated = ai.isolated_given;
    node.disableTxsMining = ai.disable_tx_mining_given;
    node.enableWebRTC = ai.enable_webrtc_given;

    if (ai.temporary_given)
        data.chaindb = "";
    if (!publicAPI && ai.enable_public_given)
        publicAPI = TCPPeeraddr("0.0.0.0:3001");

    if (ai.ws_port_given) {
        auto parse_port = [](int port) -> uint16_t {
            if (port < 0 || port > std::numeric_limits<uint16_t>::max()) {
                throw std::runtime_error("Invalid port '" + std::to_string(port) + "'");
            }
            return port;
        };
        websocketServer.port = parse_port(ai.ws_port_arg);
        if (ai.ws_tls_key_given)
            websocketServer.keyfile = ai.ws_tls_key_arg;
        if (ai.ws_tls_cert_given)
            websocketServer.certfile = ai.ws_tls_cert_arg;
        if (ai.ws_x_forwarded_for_given)
            websocketServer.XFowarded = true;
        if (ai.ws_bind_localhost_given)
            websocketServer.bindLocalhost = true;
    }
}
#endif

wrt::optional<int> ConfigParams::process_config_file(const gengetopt_args_info& ai, bool silent)
{
    std::string filename
        = is_testnet() ? "testnet_config.toml" : "config.toml";
    if (!ai.config_given && !std::filesystem::exists(filename)) {
        if (!silent)
            spdlog::debug("No config.toml file found, using default configuration");
        if (ai.test_given) {
            spdlog::error("No configuration file found.");
            return -1;
        }
    } else {
#ifndef DISABLE_LIBUV

        if (ai.config_given)
            filename = ai.config_arg;
        if (!silent)
            spdlog::info("Reading configuration file \"{}\"", filename);

        // overwrite with config file
        toml::table tbl = toml::parse_file(filename);
        TableReader root(tbl, filename);

        // db properties
        auto s_db { root.subtable("db") };
        fill(data.chaindb, s_db, "chain-db");
        fill(data.peersdb, s_db, "peers-db");
        fill(data.peersdb, s_db, "rxtx-db");

        // stratum properties
        auto s_stratum { root.subtable("stratum") };
        fill<TCPPeeraddr>(stratumPool, s_stratum, "bind");

        // publicrpc
        auto s_pubrpc { root.subtable("publicrpc") };
        fill(publicAPI, s_pubrpc, "bind");

        // jsonrpc
        auto s_jsonpc { root.subtable("jsonrpc") };
        fill(jsonrpc.bind, s_jsonpc, "bind");

        auto s_node { root.subtable("node") };
        fill(node.bind, s_node, "bind");
        fill(node.isolated, s_node, "isolated");
        fill(node.disableTxsMining, s_node, "disable-tx-mining");
        fill(peers.enableBan, s_node, "enable-ban");
        fill(peers.allowLocalhostIp, s_node, "allow-localhost-ip");
        fill(node.logCommunicationVal, s_node, "log-communication");
        fill(node.logRTC, s_node, "log-rtc");
        fill(node.snapshotSigner, s_node, "leader-key");
        fill(peers.connect, s_node, "connect");
        if (ai.test_given) {
            std::cout << "Configuration file \"" + filename + "\" is vaild.\n";
            return 0;
        }
#endif
    }
    return {};
}
int ConfigParams::init(const gengetopt_args_info& ai)
{
    try {
        bool dmp(ai.dump_config_given);
        if (!dmp) {
            spdlog::info("Warthog Node v{}.{}.{} ", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
            std::cout << "                                          %%%                \n"
                         "                            .%%%%%%%%%%%%  %%                \n"
                         "                      %%%%%%%%%%%%%%%%%%%%%%%%               \n"
                         "                       %%%%%%%%%%%%%%%%%%%%%%%#%%            \n"
                         "                  =%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%           \n"
                         "                 %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%          \n"
                         "            .%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%( )%%         \n"
                         "          .%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%       \n"
                         "        %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    \n"
                         "      %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%@\n"
                         "     %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% \n"
                         "  .%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%=   *%%%%%%%   \n"
                         "  *    %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%            \n"
                         "        %%%%%%%%%%%%%%%%%%%%%%%%%%%= %%%%%%%%%%%%%%%         \n"
                         "        %%%%%%%%%%%%%%%%%%%%%%%%   %%+       @%%%%%%%        \n"
                         "        %%%%%%%%%%%%%%%%%%%%       .%%%%%%%%.  %%%%%%%       \n"
                         "        %%%%%%%%%%%%%%%=                %%%%%%  %%%          \n"
                         "       %%%%%%%%%%%     @                 %%%%%               \n"
                         "      %%%%%%%%.   %%%%                                       \n"
                         "     %%%%%%%.  %%%%%%                                        \n"
                         "    %%%%%%.  %%%%%%                                          \n"
                         "   %%%%%.  %%%%%%%                                           \n\n";
        }

        // Log
#ifdef DISABLE_LIBUV
        assert(ConfigParams::mount_opfs("/opfs"));
#endif
        const auto warthogDir { get_default_datadir() };
        prepare_warthog_dir(warthogDir, !dmp);

        if (ai.debug_given)
            spdlog::set_level(spdlog::level::debug);

        // copy default values
        if (ai.testnet_given) {
            enable_testnet();
        }

        Endpoints mainnetEndpoints {
            "122.148.197.165:9186",
            "135.181.77.214:9186",
            "185.209.228.16:9186",
            "185.215.180.7:9186",
            "193.218.118.57:9186",
            "194.164.30.182:9186",
            "203.25.209.147:9186",
            "209.12.214.158:9186",
            "213.199.59.252:20016",
            "47.187.202.183:9186",
            "49.13.161.201:9186",
            "51.75.21.134:9186",
            "62.72.44.89:9186",
            "63.231.144.31:9186",
            "74.208.75.230:9186",
            "74.208.77.165:9186",
            "82.146.46.246:9186",
            "89.107.33.239:9186",
            "89.117.150.162:9186",
            "89.163.224.253:9186",
        };
        Endpoints testnetEndpoints {
            "193.218.118.57:9286",
            "98.71.18.140:9286"
        };

        data.chaindb = warthogDir
            + (is_testnet() ? "testnet_chain_defi.db3" : "chain_defi.db3");
        data.peersdb = warthogDir
            + (is_testnet() ? "testnet_peers.db3" : "peers_v2.db3");
        data.rxtxdb = warthogDir
            + (is_testnet() ? "testnet_rxtx.db3" : "rxtx.db3");
        jsonrpc.bind = TCPPeeraddr(is_testnet() ? "127.0.0.1:3100" : "127.0.0.1:3000");
        node.bind = TCPPeeraddr(is_testnet() ? "0.0.0.0:9286" : "0.0.0.0:9186");

        if (auto i { process_config_file(ai, dmp) })
            return *i;

#ifndef DISABLE_LIBUV
        peers.connect = is_testnet() ? testnetEndpoints : mainnetEndpoints;
        process_args(ai);
#else
        auto wsPeers { ws_peers() };
        spdlog::info("Websocket default peer list ({}):", wsPeers.size());

        size_t i = 1;
        for (auto& p : wsPeers) {
            auto a { WSUrladdr::parse(p) };
            if (a) {
                spdlog::info("Adding websocket peer {}: {}", i, p);
                peers.connect.push_back(*a);
            } else {
                spdlog::warn("Failed parsing Websocket peer {}: {}", i, p);
            }
            i += 1;
        }
#endif

        if (dmp) {
            std::cout << dump();
            return 0;
        }
    } catch (const toml::parse_error& err) {
        std::cerr << "Error while parsing file '" << *err.source().path << "':\n"
                  << err.description() << "\n  (" << err.source().begin
                  << ")\n";
        return -1;
    } catch (const std::runtime_error& e) {
        spdlog::error(e.what());
        return -1;
    }
    return 1;
}

void ConfigParams::prepare_warthog_dir(const std::string& warthogDir, bool log)
{
    if (!std::filesystem::exists(warthogDir)) {
        if (!log)
            spdlog::info("Crating Warthog directory {}", warthogDir);
        std::error_code ec;
        if (!std::filesystem::create_directories(warthogDir, ec)) {
            throw std::runtime_error("Cannot create default directory " + warthogDir + ": " + ec.message());
        }
    }
}
void ConfigParams::assign_defaults()
{
    // data.peersdb
    //         defaultDataDir
    // + (is_testnet() ? "testnet3_chain.db3" : "chain.db3"),
}
std::string ConfigParams::dump()
{
    toml::table tbl;
    tbl.insert_or_assign("jsonrpc", toml::table {
                                        { "bind", jsonrpc.bind.to_string() },
                                    });

    toml::array connect;
#ifndef DISABLE_LIBUV
    for (auto ea : peers.connect) {
        connect.push_back(ea.to_string());
    }
#endif
    tbl.insert_or_assign("stratum",
        toml::table {
            { "bind", stratumPool ? stratumPool->to_string() : ""s },
        });
    tbl.insert_or_assign("node",
        toml::table {
            { "bind", node.bind.to_string() },
            { "connect", connect },
            { "isolated", node.isolated },
            { "disable-tx-mining", node.disableTxsMining },
            { "enable-ban", peers.enableBan },
            { "allow-localhost-ip", peers.allowLocalhostIp },
            { "log-communication", (bool)node.logCommunicationVal },
            { "log-rtc", (bool)node.logRTC } });
    tbl.insert_or_assign("db", toml::table {
                                   { "chain-db", data.chaindb },
                                   { "peers-db", data.peersdb },
                                   { "rxtx-db", data.peersdb },
                               });
    stringstream ss;
    ss << tbl << endl;
    return ss.str();
}

Config::Config(ConfigParams&& params)
    : ConfigParams(std::move(params))
{
    logCommunication = node.logCommunicationVal;
    logRTC = node.logRTC;
    minMempoolFee = node.minMempoolFee;
}

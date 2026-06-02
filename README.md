[![Docker Release](https://github.com/warthog-network/Warthog/actions/workflows/release.yml/badge.svg)](https://github.com/JulsKawa/Warthog/actions/workflows/release.yml)

WARTHOG REFERENCE IMPLEMENTATION
Copyright (c) 2023 - 2024 Pumbaa, Timon & Rafiki
<p align="center">
  <img src="doc/img/warthog_logo.png" style="width:300px;"/>
</p>

# 🐗 Warthog Reference Implementation


Warthog is an experimental innovative cryptocurrency (a *fresh* rewrite, no fork!!), which tries to push the boundaries of what is possible in the crypto-industry.
As a small team of volunteers and crypto enthusiasts we are creatively developing Warthog with major features the world has not seen before:

- **Thread-based sync model**: Unlike traditional cryptocurrencies where blocks are identified and downloaded by their hash we have implemented a thread-based approach. Technically, a block chain with possible forks is a tree, and by implementing a branch-aware sync algorithm, we can address blocks by height and branch. This shrinks byte size of communication messages and also improves latency for distributing new blocks which is one of the reasons why sync time in Warthog is faster than in other cryptocurrencies. Furthermore nodes are aware of the sync state of peers. To the best of our knowledge Warthog is the first cryptocurrency to implement this approach.

- **Crypto Research** Proof of Balanced Work (by CoinFuMasterShifu):
    Proof of Balanced Work (PoBW) is a novel technique invented by CoinFuMasterShifu to combine different hash functions in a balanced way. For efficient mining, one cannot ignore any of the combined algorithms, all must be mined and their hashrates determine the combined hashrate. There is a scientific research paper on general PoBW here: https://raw.githubusercontent.com/CoinFuMasterShifu/ProofOfBalancedWork/main/PoBW.pdf

- **Janushash PoBW mining algorithm**:
    Warthog's Janushash mining algorithm combines two algorithms, namely Sha256t and Verushash v2.1 (will soon switch to v2.2). Efficient mining this combination requires a GPU and a CPU. This implies that mainly APUs are used for mining while existing GPU farms or CPU farms are out of the game. The implication of this cannot be overestimated as this brings mining in Warthog closer to Satoshi's original dream "One computer, one vote" than any other cryptocurrency: Firstly, APUs are pretty cheap and widely available such that everyone can participate in mining and secondly, no CPU botnet, nor any GPU farm will suddenly appear and disrupt prices or diminish people's mining revenue.

- **Browser-based nodes** (by CoinFuMasterShifu), work in progress:
    Warthog is the first cryptocurrency with nodes that can run entirely in the browser. This is a big achievement since it lowers the boundary of setting up a node as low as opening a website, which is even possible on smartphones, and it also improves decentralization as these are full nodes. Warthog achieves this by using bleeding edge technology (WASM FS using OPFS supported by recent browsers and SQLite's recently added support to work with OPFS).

## Roadmap

- **WebRTC communication between browser nodes**:
    WebRTC is a technology which allows direct communication between browsers after connection is established with the help of a server. We aim to set up several official nodes which assist in establishing direct peer to peer WebRTC connections to other nodes which then can themselves assist in establishing additional WebRTC connections without using the official nodes' help. This will allow for a smartphone peer-to-peer network of full nodes within the browser contributing to decentralization and network resilience.

- **Asset support and hard-coded DeFi**: Warthog will support a tailored DeFi implementation which allows to create and place assets. Direct custom tailored hard-coded DeFi implementation has several advantages over indirect smart contract based approaches: Firstly, the attack surface is much lower (DeFi platforms based on smart contracts are often hacked with funds being stolen) and secondly, the whole database architecture can be designed such that transactions will be more space-efficient and better native support for assets and orders can be offered which lowers adoption barrier and might allow for more convenience features to list all owned assets directly using node API. Furthermore new features like crowd-funding new assets with fair proportional distribution, creating new assets with balance distribution copied from other assets, paying dividends to all holders of some assets are possible when using tailored hard-coded design. Finally, end users will benefit from clear standardization of native DeFi support as a first-class-citizen since obscure and unfair practices (like increase of supply and additional toke-specific fees for trading) as used by most scam or pump-and-dump projects will not be possible.




## 💵 Tokenomics
We are a cryptocurrency for the community where everyone can revive again the good old days when crypto was fun. Therefore we have decided to be free of 💩bullshit:
- 👍 No Premine
- 🤟 No Team/Dev fund
- 😊 100% of supply is publicly mineable
<p align="center">
  <img src="doc/img/tokenomics.png" />
</p>

The block chain has the following characteristics:
- 1️⃣  Coin unit: 1 WART
- ⏲  Block time: 20s
- 💰 Initial block reward: 3 WART
- 🧮 Precision: 0.00000001 WART (8 digits)
- 🔪 Halving: ~ every 2 years
- 🔒 Supply hard cap : 18921599.68464 (~19m) WART


## 💣 A word of caution
This is new software. Almost everything was 
implemented from scratch. There may be bugs.
Use at your own risk.


## 📢 Socials

<p align="center">
<a href="https://discord.gg/QMDV8bGTdQ"><img src="doc/img/discord.png" alt="drawing" style="width:16px;"/> Discord </a>
| 
<a href="https://t.me/warthognetwork"><img src="doc/img/telegram.png" alt="drawing" style="width:16px;"/> Telegram </a>
| 
 <a href="https://bitcointalk.org/index.php?topic=5458046.0"> <img src="doc/img/bitcointalk.png" alt="drawing" style="width:16px;"/> Bitcointalk</a>
 |
 <a href="http://warthog.network">🌐 Website</a>
</p>

## 💱 Where to buy?
- P2P on Discord<br>
- <a href="https://exbitron.com/">Exbitron exchange</a><br>
- <a href="https://xeggex.com/market/WART_USDT">Xeggex exchange</a>
- <a href="https://tradeogre.com/exchange/WART-USDT">TradeOgre exchange</a>
- <a href="https://www.coinex.com/en/exchange/WART-USDT">CoinEx exchange</a>
- <a href="https://www.bit.com/spot?pair=WART-USDT">Bit.com Exchange</a>
- <a href="https://bitcointry.com/en/exchange/WART_USDT">Bitcointry Exchange</a>


## 📦 Component overview

### This Repo
* Reference node implementation of the Warthog Network
* Command line wallet software

### Miner
* GPU/CPU Miner for JanusHash [here](https://github.com/CoinFuMasterShifu/janusminer)

### Additional Tools:
* GUI wallet [here](https://github.com/warthog-network/wart-wallet)
 
## 💻 Installation
Prebuilt binaries of the node daemon and cli wallet for Linux and Windows can be downloaded [here](https://github.com/warthog-network/Warthog/releases). They are staticlly linked and will just work without external dependencies.

Prebuilt binaries of the miner for Linux and HiveOS can be downloaded [here](https://github.com/CoinFuMasterShifu/janusminer/releases)

To compile from source see below or [here](https://docs.warthog.network/guides/node/compiling/) for a more detailed guide.

## 😵‍💫 BUILD INSTRUCTIONS

### Linux Native Build

#### System Requirements

* Linux
* gcc11 or newer
* meson
* ninja

#### Required Steps
* Install gcc, meson, ninja: apt install meson ninja-build build-essential
* Clone the repo: `git clone https://github.com/ByPumbaa/Warthog`
* cd into the repo: `cd Warthog`
* Create build directory: `meson build .` (`meson build . --buildtype=release` for better performance)
* cd into build directory: `cd build`
* Compile using ninja: `ninja`

### Docker build (node and wallet)
#### System Requirements
* Linux
* Docker

#### Build for Linux
* Run `DOCKER_BUILDKIT=1 docker build . -f dockerfiles/build_linux --output build` in the repo directory.
#### Build for Windows (cross-compilation on Linux)
* Run `DOCKER_BUILDKIT=1 docker build . -f dockerfiles/build_windows --output ./build/windows` in the repo.
* Windows binaries are located in `./build/windows` directory.
#### Build for MacOS - aarch64 (cross-compilation on Linux)
* Run `DOCKER_BUILDKIT=1 docker build . -f dockerfiles/build_macos --output ./build/macos` in the repo.
* MacOS binaries are located in `./build/macos` directory.

### Justfile recipes
For development tasks, this project uses a [just](https://github.com/casey/just)file.

#### System Requirements
* [just](https://github.com/casey/just#installation)

#### Available recipes
* `just build_debug` - Build debug Docker image
* `just bump` - Bump patch version in meson.build (e.g., 0.10.4 → 0.10.5)

## ▶️ USAGE
* Run the node (use some restarter in case it crashes) <br />
One line example to run the node: `screen -dmS wart_node bash -c "while true; do ./wart-node-linux ; done"` <br />
Use `screen -r wart_node` to see its output and CTRL+A+D to detach from the screen session. <br />
Note: You should run node with  `--rpc=0.0.0.0:3000` to accept remote connections from your other rigs. <br />
* Run the miner (miner requires node running). 
More detailed information how to set up and run the miner you can find [here](https://github.com/CoinFuMasterShifu/janusminer/blob/master/README.md).
* Optional: Run the wallet to send funds (wallet requires node running)
* Good luck and have fun! Use --help the option.

NOTE:  This is a highly experimental project not backed by any institution or foundation. 
 It relies on the work of voluntaries who have no obligation to do work for the project.
 People can join and leave any time at their will.

## 📖 Documentation
* [API Reference](https://docs.warthog.network/developers/api/)
* [Wallet Integration Guide](https://docs.warthog.network/developers/integrations/wallet-integration/)
* [Pool Integration Guide](https://docs.warthog.network/developers/integrations/pools/)
* [Miner Integration Guide](https://docs.warthog.network/developers/integrations/miners/)
* [Janushash algorithm](https://docs.warthog.network/janushash/interpreting-hashes-as-numbers/). First Proof of Balanced Work mining algorithm in the world.

## Useful Links
* [See here](https://docs.warthog.network/links/)



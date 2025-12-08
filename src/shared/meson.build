
include_wh=[include_trezorcrypto,include_directories('./src')]

src_defi = files([
    './src/defi/uint64/matcher.cpp',
    './src/defi/uint64/orderbook.cpp',
])

src_wh=[
    src_trezorcrypto,
    src_defi,
  files(
  [
    './platformassert.cpp',
    './src/block/block.cpp',
    './src/block/body/body.cpp',
    './src/block/body/container.cpp',
    './src/block/body/merkle_write_hooker.cpp',
    './src/block/body/nonce.cpp',
    './src/block/body/transaction_id.cpp',
    './src/block/chain/height.cpp',
    './src/block/chain/worksum.cpp',
    './src/block/header/generator.cpp',
    './src/block/header/header.cpp',
    './src/block/header/header.cpp',
    './src/block/header/pow_version.cpp',
    './src/block/header/view.cpp',
    './src/block/version.cpp',
    './src/communication/create_transaction.cpp',
    './src/crypto/address.cpp',
    './src/crypto/crypto.cpp',
    './src/crypto/hash.cpp',
    './src/crypto/verushash/verus_clhash_port.cpp',
    './src/crypto/verushash/verushash.cpp',
    './src/defi/token/asset.cpp',
    './src/defi/uint64/orderbook.cpp',
    './src/defi/uint64/price.cpp',
    './src/general/compact_uint.cpp',
    './src/general/errors.cpp',
    './src/general/funds.cpp',
    './src/general/hex.cpp',
    './src/general/is_testnet.cpp',
    './src/general/structured_reader.cpp',
    './src/general/with_uint64.cpp',
  ]
  )]


versions = meson.project_version().split('.')
conf_version = configuration_data()
conf_version.set('VERSION_MAJOR', versions.get(0))
conf_version.set('VERSION_MINOR', versions.get(1))
conf_version.set('VERSION_PATCH', versions.get(2))
configure_file(input : './src/version_def.hpp.in',
               output : 'version_def.hpp',
               configuration : conf_version)


vcs_dep=vcs_tag(command : 
  ['git', 'rev-parse', '--short', 'HEAD'],
  input:'./src/commit_info.hpp.in',
  output:'commit_info.hpp',
  replace_string:'@GIT_COMMIT_INFO@')

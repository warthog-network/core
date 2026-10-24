
# The generated headers live in the build directory of this subproject:
# version_def.hpp and commit_info.hpp. They are included by name, so the
# build directory has to be on the include path.
include_wh=[include_trezorcrypto,include_directories('./src'),include_directories('.')]

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
    './src/api/http/json_converter.cpp',
    './src/api/types/shared.cpp',
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
    './src/transport/helpers/ip.cpp',
    './src/transport/helpers/ipv4.cpp',
    './src/transport/helpers/ipv6.cpp',
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


# The commit the build was made from.
#
# It comes from the environment, because the build runs in a container
# that has no git and no repository to ask. The host computes it and
# passes it in with --build-arg. Without it the build says so, rather
# than repeating the version and pretending it is a commit.
commit = run_command(
  'sh', '-c', 'printf %s "${WARTHOG_COMMIT:-}"',
  check : false).stdout().strip()

conf_commit = configuration_data()
conf_commit.set('GIT_COMMIT_INFO', commit == '' ? 'unknown' : commit)
configure_file(input : './src/commit_info.hpp.in',
               output : 'commit_info.hpp',
               configuration : conf_commit)

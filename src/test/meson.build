e = executable('custom_float', ['./custom_float.cpp'],
  include_directories:['./' ,include_thirdparty]
  )
test('Custom float operations',e)

# The merkle root in the block header must fixate every body byte, otherwise a
# tampered body passes the root check and is then reported as a bad header.
if not get_option('emscripten')
e = executable('body_merkle', ['./body_merkle.cpp', src_wh],
  include_directories:[include_wh, include_thirdparty],
  link_with:[libsecp256k1],
  dependencies:[json_dep, spdlog_dep, libuv_dep]
  )
test('Block body merkle tree',e)
endif


deps = [json_dep, spdlog_dep]
if not get_option('emscripten')
deps = [deps, libuv_dep]
endif

executable('wart-wallet', vcs_dep, 
  [
    './api_call.cpp', 
    './cmdline/cmdline.cpp', 
    './main.cpp', 
    src_wh, 
],
include_directories : [include_secp256k1,include_wh, include_httplib],
link_with: libsecp256k1, 
dependencies: deps,
install : true)

executable('wart-wallet', 
  [
    './api_call.cpp', 
    './cmdline/cmdline.cpp', 
    './main.cpp', 
    src_wh
  ],
  include_directories : [include_secp256k1,include_wh,include_json, include_httplib],
  link_with: libsecp256k1, 
  dependencies: [libuv_dep,spdlog_dep],
  install : true)

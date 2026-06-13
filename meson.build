project( 'Warthog', ['c','cpp'],
  version : '0.10.10',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

rtcstatic = []
if host_machine.system() == 'windows'
  rtcstatic = '-DRTC_STATIC'
endif

json_dep = subproject('json', default_options : ['warning_level=0', 'werror=false']).get_variable('nlohmann_json_dep')
spdlog_dep = subproject('spdlog', default_options : ['warning_level=0', 'werror=false']).get_variable('spdlog_dep')

if not get_option('emscripten')
libuv_dep = subproject('libuv', default_options : ['warning_level=0', 'werror=false', 'build_tests=false']).get_variable('libuv_dep')
libwebsockets_dep = subproject('libwebsockets-4.3.3', default_options:['warning_level=0', 'werror=false','uv=enabled']).get_variable('libwebsockets_dep')
libjuice_dep = subproject('libjuice', default_options : ['warning_level=0', 'werror=false']).get_variable('libjuice_dep')
libmbedtls_dep = subproject('mbedtls', default_options : ['warning_level=0', 'werror=false']).get_variable('libmbedtls_dep')
usrsctp_dep = subproject('usrsctp', default_options : ['warning_level=0', 'werror=false']).get_variable('usrsctp_dep')
plog_dep = subproject('plog', default_options : ['warning_level=0', 'werror=false']).get_variable('plog_dep')
libdatachannel_dep = subproject('libdatachannel', default_options : ['warning_level=0', 'werror=false']).get_variable('libdatachannel_dep')
libmbedtls_dep = subproject('mbedtls', default_options : ['warning_level=0', 'werror=false']).get_variable('libmbedtls_dep')

# exe = executable('rtc_server', 'rtc_server.cpp',
#   dependencies: [
#     json_dep,
#     libdatachannel_dep,
#     ],
#   install : true)
# exe = executable('rtc_client', 'rtc_client.cpp',
#   dependencies: [
#     json_dep,
#     libdatachannel_dep,
#     ],
#   install : true)
else
libdatachannel_dep = subproject('datachannel-wasm', default_options : ['warning_level=0', 'werror=false']).get_variable('libdatachannelwasm_dep')
exe = executable('rtc_server', 'rtc_server.cpp',
  dependencies: [
    libdatachannel_dep,
    json_dep,
    ],
  link_args:[
    '-pthread',
    '-sNO_DISABLE_EXCEPTION_CATCHING',
    # '-sSTACK_SIZE=10mb', 
    '-fexceptions', '-sPROXY_TO_PTHREAD',
    # '-sUSE_CLOSURE_COMPILER=0', '-sIMPORTED_MEMORY',
    # '-sWASM_BIGINT=1', '-sMODULARIZE', '-sDYNAMIC_EXECUTION=0', '-sNO_POLYFILL',
    # '-sSTRICT_JS=0', '-sALLOW_MEMORY_GROWTH', '-sINITIAL_MEMORY=16777216', '-sSTACK_SIZE=2048KB', '-sGLOBAL_BASE=4096', '-sALLOW_TABLE_GROWTH',
    # '-sERROR_ON_UNDEFINED_SYMBOLS=1', '-sENVIRONMENT=web,worker',
    # '-sWASMFS', '-sPTHREAD_POOL_SIZE=1', '-sLLD_REPORT_UNDEFINED', '-sEXPORT_ES6', '-sUSE_ES6_IMPORT_META'
    ],
  # name_suffix: 'html',
  install : true)
endif
sqlite3_dep = subproject('sqlite3',default_options : ['warning_level=0', 'werror=false']).get_variable('sqlite3_dep')

subdir('./thirdparty')
subdir('./src/shared')
subdir('./src/node')
# subdir('./src/wallet')
subdir('./src/tui_wallet')
subdir('./src/test')

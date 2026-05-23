project( 'Warthog', ['c','cpp'],
  version : '0.8.16',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

libuv_dep = subproject('libuv', default_options : ['warning_level=0', 'werror=false', 'build_tests=false']).get_variable('libuv_dep')
sqlite3_dep = subproject('sqlite3',default_options : ['warning_level=0', 'werror=false']).get_variable('sqlite3_dep')

subdir('./thirdparty')
subdir('./src/shared')
subdir('./src/node')
subdir('./src/wallet')
subdir('./src/test')

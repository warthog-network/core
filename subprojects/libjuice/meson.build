project('libjuice',
    'c',
    version : '1.7.4',
    license : 'Mozilla Public License Version 2.0',
)

sources = files(
	'src/addr.c',
	'src/agent.c',
	'src/crc32.c',
	'src/const_time.c',
	'src/conn.c',
	'src/conn_poll.c',
	'src/conn_thread.c',
	'src/conn_mux.c',
	'src/base64.c',
	'src/hash.c',
	'src/hmac.c',
	'src/ice.c',
	'src/tcp.c',
	'src/juice.c',
	'src/log.c',
	'src/random.c',
	'src/server.c',
	'src/stun.c',
	'src/timestamp.c',
	'src/turn.c',
	'src/udp.c')

inc_dirs = include_directories('./include/juice')
thread_dep = dependency('threads')
libjuice = library(
  'libjuice',
  sources,
  dependencies: thread_dep,
  c_args: ['-DJUICE_STATIC'],
  include_directories: inc_dirs
)

libjuice_dep = declare_dependency(
    include_directories: './include',
    link_with: libjuice
)

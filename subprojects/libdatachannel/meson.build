project('libdatachannel',
    'cpp',
    version : '0.24.6',
    license : 'Apache-2.0/GPL-2.0-or-later ',
    default_options : ['cpp_std=c++20'],
)

sources = files(
	'src/candidate.cpp',
	'src/channel.cpp',
	'src/configuration.cpp',
	'src/datachannel.cpp',
	'src/description.cpp',
	'src/mediahandler.cpp',
	'src/message.cpp',
	'src/peerconnection.cpp',
	'src/track.cpp',
	'src/rtp.cpp',
	'src/impl/certificate.cpp',
	'src/impl/channel.cpp',
	'src/impl/datachannel.cpp',
	'src/impl/dtlstransport.cpp',
	'src/impl/icetransport.cpp',
	'src/impl/init.cpp',
	'src/impl/peerconnection.cpp',
	'src/impl/logcounter.cpp',
	'src/impl/sctptransport.cpp',
	'src/impl/threadpool.cpp',
	'src/impl/tls.cpp',
	'src/impl/track.cpp',
	'src/impl/utils.cpp',
	'src/impl/transport.cpp',
	'src/impl/processor.cpp')

plog_dep = subproject('plog', default_options : ['warning_level=0', 'werror=false']).get_variable('plog_dep')
libjuice_dep = subproject('libjuice', default_options : ['warning_level=0', 'werror=false']).get_variable('libjuice_dep')
usrsctp_dep = subproject('usrsctp', default_options : ['warning_level=0', 'werror=false']).get_variable('usrsctp_dep')
libmbedtls_dep = subproject('mbedtls', default_options : ['warning_level=0', 'werror=false']).get_variable('libmbedtls_dep')
thread_dep = dependency('threads')



inc_dirs = include_directories('./include/')
libdatachannel = library(
  'libdatachannel',
  sources,
  cpp_args: ['-DMBEDTLS_SSL_DTLS_SRTP', '-DUSE_MBEDTLS',  '-DRTC_ENABLE_MEDIA=0', '-DRTC_ENABLE_WEBSOCKET=0', '-DRTC_EXPORTS', '-DJUICE_STATIC'],
  dependencies: [
    plog_dep,
    libmbedtls_dep,
    usrsctp_dep,
    thread_dep,
    libjuice_dep],
  include_directories: ['./src', './include/', './include/rtc']
)


winlink = []
if host_machine.system() == 'windows'
  winlink = ['-lbcrypt']
endif

libdatachannel_dep = declare_dependency(
    include_directories: inc_dirs,
    link_with: libdatachannel,
    link_args: winlink
)

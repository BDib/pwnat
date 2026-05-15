project('pwnat', 'c',
  version : '2.0',
  default_options : ['c_std=c11', 'warning_level=2'])

cc = meson.get_compiler('c')
add_project_arguments('-D_GNU_SOURCE', language : 'c')

deps = []
deps += dependency('nice')
deps += dependency('glib-2.0')
deps += dependency('gobject-2.0')

if cc.has_function('nsl', prefix : '#include <netdb.h>')
    deps += cc.find_library('nsl', required : false)
endif
if cc.has_function('socket', prefix : '#include <sys/socket.h>')
    deps += cc.find_library('socket', required : false)
endif
deps += cc.find_library('resolv', required : false)

if host_machine.system() == 'windows'
  add_project_arguments('-D_WIN32', language : 'c')
  deps += cc.find_library('ws2_32')
  os_sources = ['windoze.c']
elif host_machine.system() == 'darwin'
  add_project_arguments('-DAPPLE', language : 'c')
  os_sources = []
elif host_machine.system() == 'sunos'
  add_project_arguments('-DSOLARIS', language : 'c')
  os_sources = []
else
  add_project_arguments('-DLINUX', language : 'c')
  os_sources = []
endif

add_project_arguments('-DHAVE_NICE', language : 'c')

sources = [
  'pwnat.c',
  'socket.c',
  'message.c',
  'strlcpy.c',
  'client.c',
  'packet.c',
  'list.c',
  'destination.c',
  'udpserver.c',
  'udpclient.c',
  'ice_transport.c',
  'relay.c'
] + os_sources

executable('pwnat', sources,
  dependencies : deps,
  install : true)

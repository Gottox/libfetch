project(
	'libfetch',
	['c'],
	default_options : [
    'c_std=gnu17',
		'optimization=2',
    'werror=false',
    'warning_level=0'
	],
	version: '0.0.0',
	meson_version: '>=0.57'
)

header = [
  'common.h',
  'fetch.h'
]

source = [
	'file.c',
	'ftp.c',
	'http.c',
	'common.c',
	'fetch.c',
]

build_args = [
  '-D_FILE_OFFSET_BITS=64',
  '-D_LARGEFILE_SOURCE',
  '-D_LARGE_FILES',
  '-DFTP_COMBINE_CWDS',
  '-DNETBSD',
]

dependencies = [
  dependency('threads')
]

if get_option('openssl')
  dependencies += dependency('openssl')
  build_args += '-DWITH_SSL'
endif

if get_option('inet6')
  build_args += '-DINET6'
endif

subproject = meson.is_subproject()

errlist_sh = find_program('./errlist.sh')
foreach prot : [ 'ftp', 'http' ]
  header += custom_target(prot+'err.h',
    output: prot+'err.h',
    input: prot+'.errors',
    command: [errlist_sh, prot+'_errlist', prot.to_upper(), '@INPUT@'],
    capture: true
  )
endforeach

library('fetch',
  install: not subproject,
  source + header,
  c_args: build_args,
  dependencies: dependencies
)

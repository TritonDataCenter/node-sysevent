{
	"targets": [
		{
			"target_name": "module",
			"cflags": [
				"-Wall",
				"-Wextra",
				"-Werror",
			],
			"xcode_settings": {
				"OTHER_CFLAGS": [
					"-Wall",
					"-Wextra",
					"-Werror",
				]
			},
			"sources": [
				"src/module.cc",
				"src/more.c",
				"src/illumos_list.c",
				"src/crossthread.c"
			],
			"libraries": [
				"-lnvpair",
				"-lsysevent"
			],
			"include_dirs": [
				"<!(node -e \"require('nan')\")"
			]
		}
	]
}

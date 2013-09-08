project "bspc"
	targetname  "bspc"
	language    "C++"
	kind        "ConsoleApp"
	files
	{
		"**.c", "**.cpp", "**.h",
		
		"../../engine/qcommon/cm_load.c",
		"../../engine/qcommon/cm_patch.c",
		"../../engine/qcommon/cm_test.c",
		"../../engine/qcommon/cm_trace.c",
		
		"../../engine/botlib/be_aas_bspq3.c",
		"../../engine/botlib/be_aas_cluster.c",
		"../../engine/botlib/be_aas_move.c",
		"../../engine/botlib/be_aas_optimize.c",
		"../../engine/botlib/be_aas_reach.c",
		"../../engine/botlib/be_aas_sample.c",		
		"../../engine/botlib/l_libvar.c",
		"../../engine/botlib/l_precomp.c",
		"../../engine/botlib/l_script.c",
		"../../engine/botlib/l_struct.c",
		
		"../../engine/qcommon/md4.c",
		"../../engine/qcommon/unzip.c",
		
		"../../libs/zlib/**.c", "../../../libs/zlib/**.h",
	}
	includedirs
	{
		"../../engine/qcommon",
		"../../engine/botlib/",
		"../../libs/zlib",
	}
	defines
	{ 
		"BSPC", 
	}	
	
	--
	-- Platform Configurations
	-- 	
	--configuration "x32"
	--	targetdir 	"../../../bin32"
	
	--configuration "x64"
	--	targetdir 	"../../../bin64"
	
	
	-- 
	-- Project Configurations
	-- 
	configuration "vs*"
		linkoptions
		{
			"/LARGEADDRESSAWARE",
		}
		defines
		{
			"WIN32",
			"_CRT_SECURE_NO_WARNINGS",
			--"USE_INTERNAL_SPEEX",
			--"USE_INTERNAL_ZLIB",
			--"FLOATING_POINT",
			--"USE_ALLOCA"
		}
		
	configuration { "vs*", "x32" }
		targetdir 	"../../../bin/win32"
		
	configuration { "vs*", "x64" }
		targetdir 	"../../../bin/win64"

	configuration { "linux", "x32" }
		targetdir 	"../../../bin/linux-x86"
		
	configuration { "linux", "x64" }
		targetdir 	"../../../bin/linux-x86_64"

	configuration { "linux", "native" }
		targetdir 	"../../../bin/linux-native"
	
	configuration "linux"
		targetname  "bspc"
		

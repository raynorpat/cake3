
project "XMap2"
	targetname  "XMap2"
	language    "C++"
	kind        "ConsoleApp"
	files
	{		
		"../common/cmdlib.c", "../common/cmdlib.h",
		"../common/imagelib.c", "../common/imagelib.h",
		"../common/inout.c", "../common/inout.h",
		"../common/mathlib.c", "../common/mathlib.h",
		"../common/mutex.c", "../common/mutex.h",
		"../common/polylib.c", "../common/polylib.h",
		"../common/scriplib.c", "../common/scriplib.h",
		"../common/threads.c", "../common/threads.h",
		"../common/unzip.c", "../common/unzip.h",
		"../common/vfs.c", "../common/vfs.h",
		
		"**.c", "**.cpp", "**.h",
		
		"../../libs/picomodel/**.c", "../../libs/picomodel/**.h",
		"../../libs/picomodel/lwo/**.c", "../../libs/picomodel/lwo/**.h",
		
		"../../libs/jpeg/**.c", "../../../libs/jpeg/**.h",
		"../../libs/png/**.c", "../../../libs/png/**.h",
		"../../libs/zlib/**.c", "../../../libs/zlib/**.h",
		--"../libs/openexr/**.cpp", "../../libs/openexr/**.h",
	}
	includedirs
	{
		"../common",
		"../../libs/picomodel",
		"../../libs/png",
		"../../libs/zlib",
	}
	defines
	{ 
		--"USE_XML", 
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
		--flags       {"WinMain"}
		files
		{
			"xmap2.ico",
			"xmap2.rc",
		}
		includedirs
		{
			"../../libs/glib/include/glib-2.0",
			"../../libs/glib/lib/glib-2.0/include",
			"../../libs/sdl2/include",
		}
		links
		{ 
			"wsock32",
			"glib-2.0",
		}
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
		defines
		{
			"USE_OPENGL",
		}
		libdirs
		{
			"../../libs/glib/lib",
			"../../libs/sdl2/lib",
		}
		links
		{ 
			"wsock32",
			"SDL2",
			"SDL2main",
			"opengl32",
			"glu32",
		}
		
	configuration { "vs*", "x64" }
		targetdir 	"../../../bin/win64"
		libdirs
		{
			"../../libs/glib/lib64",
			"../../libs/sdl2/lib64",
		}
	
	configuration { "linux", "gmake" }
		defines
		{
			"USE_OPENGL",
		}
		buildoptions
		{
			"`pkg-config --cflags glib-2.0`",
			"`pkg-config --cflags sdl2`",
		}
		linkoptions
		{
			"`pkg-config --libs glib-2.0`",
			"`pkg-config --libs sdl2`",
		}
		
	configuration { "linux", "x32" }
		targetdir 	"../../../bin/linux-x86"
		
	configuration { "linux", "x64" }
		targetdir 	"../../../bin/linux-x86_64"

	configuration { "linux", "native" }
		targetdir 	"../../../bin/linux-native"
	
	configuration "linux"
		targetname  "xmap2"
		links
		{
			"GL",
			"GLU",
		}
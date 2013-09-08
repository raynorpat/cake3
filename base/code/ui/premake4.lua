project "base_ui"
	targetname  "ui"
	targetdir 	"../.."
	language    "C++"
	kind        "SharedLib"
	files
	{
		"../../../code/shared/**.c",
		"../../../code/shared/q_shared.h",
		"../../../code/shared/ui_public.h",
		"../../../code/shared/tr_types.h",
		"../../../code/shared/keycodes.h",
		"../../../code/shared/surfaceflags.h",
		
		"**.c", "**.cpp", "**.h",
	}
	includedirs
	{
		"../../../code/shared",
	}
	defines
	{ 
		"UI",
	}
	
	--
	-- Platform Configurations
	--
	configuration "x32"
		targetname  "uix86"
	
	configuration "x64"
		targetname  "uix86_64"
				
	configuration "native"
		targetname  "uix86_64"
				
	-- 
	-- Project Configurations
	-- 
	configuration "vs*"
		linkoptions
		{
			"/DEF:ui.def",
		}
		defines
		{
			"WIN32",
			"_CRT_SECURE_NO_WARNINGS",
		}
	
	configuration { "linux", "x32" }
		targetname  "uii386"
		targetprefix ""
	
	configuration { "linux", "x64" }
		targetname  "uix86_64"
		targetprefix ""
	
	configuration { "linux", "native" }
		targetname  "uix86_64"
		targetprefix ""
	

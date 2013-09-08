project "base_game"
	targetname  "game"
	targetdir 	"../.."
	language    "C++"
	kind        "SharedLib"
	files
	{
		"../../../code/shared/**.c",
		"../../../code/shared/q_shared.h",
		"../../../code/shared/g_public.h",
		"../../../code/shared/surfaceflags.h",
		
		--"**.c", "**.cpp", "**.h",
		"*.h",
		"bg_misc.c",
		"bg_pmove.c",
		"bg_slidemove.c",
		"g_active.c",
		"g_arenas.c",
		"g_bot.c",
		"g_client.c",
		"g_cmds.c",
		"g_combat.c",
		"g_explosive.c",
		"g_items.c",
		"g_lua.c",
		"g_main.c",
		"g_mem.c",
		"g_misc.c",
		"g_missile.c",
		"g_mover.c",
		"g_session.c",
		"g_spawn.c",
		"g_svcmds.c",
		"g_syscalls.c",
		"g_target.c",
		"g_team.c",
		"g_trigger.c",
		"g_utils.c",
		"g_weapon.c",
		"lua_*.c",
		
		"../../../code/libs/lua/src/lapi.c",
		"../../../code/libs/lua/src/lcode.c",
		"../../../code/libs/lua/src/ldebug.c",
		"../../../code/libs/lua/src/ldo.c",
		"../../../code/libs/lua/src/ldump.c",
		"../../../code/libs/lua/src/lfunc.c",
		"../../../code/libs/lua/src/lgc.c",
		"../../../code/libs/lua/src/llex.c",
		"../../../code/libs/lua/src/lmem.c",
		"../../../code/libs/lua/src/lobject.c",
		"../../../code/libs/lua/src/lopcodes.c",
		"../../../code/libs/lua/src/lparser.c",
		"../../../code/libs/lua/src/lstate.c",
		"../../../code/libs/lua/src/lstring.c",
		"../../../code/libs/lua/src/ltable.c",
		"../../../code/libs/lua/src/ltm.c",
		"../../../code/libs/lua/src/lundump.c",
		"../../../code/libs/lua/src/lvm.c",
		"../../../code/libs/lua/src/lzio.c",
		"../../../code/libs/lua/src/lauxlib.c",
		"../../../code/libs/lua/src/lbaselib.c",
		"../../../code/libs/lua/src/ldblib.c",
		"../../../code/libs/lua/src/liolib.c",
		"../../../code/libs/lua/src/lmathlib.c",
		"../../../code/libs/lua/src/ltablib.c",
		"../../../code/libs/lua/src/lstrlib.c",
		"../../../code/libs/lua/src/loadlib.c",
		"../../../code/libs/lua/src/linit.c",
		"../../../code/libs/lua/src/loslib.c",
	}
	--excludes
	--{
	--	"g_rankings.c",
	--	"g_bullet.cpp",
	--}
	includedirs
	{
		"../../../code/shared",
		"../../../code/libs/lua/src",
	}
	defines
	{ 
		"QAGAME",
		"LUA"
	}
	
	configuration "with-bullet"
		files
		{
			"g_bullet.cpp",
		
			"../../../code/libs/bullet/*.h",
			"../../../code/libs/bullet/LinearMath/**.cpp", "../../../code/libs/bullet/LinearMath/**.h",
			"../../../code/libs/bullet/BulletCollision/**.cpp", "../../../code/libs/bullet/BulletCollision/**.h",
			"../../../code/libs/bullet/BulletDynamics/**.cpp", "../../../code/libs/bullet/BulletDynamics/**.h",
			"../../../code/libs/bullet/BulletSoftBody/**.cpp", "../../../code/libs/bullet/BulletSoftBody/**.h",
		}
		includedirs
		{
			"../../../code/libs/bullet"
		}
		defines
		{ 
			"USE_BULLET"
		}
		
	configuration "with-acebot"
		files
		{
			"acebot/**.c", "acebot/**.cpp", "acebot/**.h",
		}
		includedirs
		{
			"../game/acebot"
		}
		defines
		{ 
			"ACEBOT"
		}
		
	configuration "with-brainworks"
		files
		{
			"brainworks/**.c", "brainworks/**.cpp", "brainworks/**.h",
		}
		includedirs
		{
			"../game/brainworks"
		}
		defines
		{ 
			"BRAINWORKS"
		}		
	
	--
	-- Platform Configurations
	--
	configuration "x32"
		targetname  "qagamex86"
	
	configuration "x64"
		targetname  "qagamex86_64"
				
	configuration "native"
		targetname  "qagamex86_64"
				
	-- 
	-- Project Configurations
	-- 
	configuration "vs*"
		linkoptions
		{
			"/DEF:game.def",
		}
		defines
		{
			"WIN32",
			"_CRT_SECURE_NO_WARNINGS",
		}
	
	configuration { "linux", "x32" }
		targetname  "qagamei386"
		targetprefix ""
	
	configuration { "linux", "x64" }
		targetname  "qagamex86_64"
		targetprefix ""
	
	configuration { "linux", "native" }
		targetname  "qagamex86_64"
		targetprefix ""

	
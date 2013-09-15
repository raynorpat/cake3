--
-- XreaL build configuration script
-- 
solution "XreaL"
	configurations { "Release", "Debug" }
	platforms {"x32", "x64", "native"}
	
	--
	-- Release/Debug Configurations
	--
	configuration "Release"
		defines     "NDEBUG"
		flags      
		{
			"OptimizeSpeed",
			"EnableSSE"
		}
	
	configuration "Debug"
		defines     "_DEBUG"
		flags
		{
			"Symbols"
		}
	
--
-- Options
--
newoption
{
	trigger = "with-bullet",
	description = "Compile with Bullet physics game code support"
}

newoption
{
	trigger = "with-acebot",
	description = "Compile with AceBot game code support"
}

--newoption
--{
--	trigger = "with-freetype",
--	description = "Compile with freetype support"
--}
		
--newoption
--{
--	trigger = "with-openal",
--	value = "TYPE",
--	description = "Specify which OpenAL library",
--	allowed = 
--	{
--		{ "none", "No support for OpenAL" },
--		{ "dlopen", "Dynamically load OpenAL library if available" },
--		{ "link", "Link the OpenAL library as normal" },
--		{ "openal-dlopen", "Dynamically load OpenAL library if available" },
--		{ "openal-link", "Link the OpenAL library as normal" }
--	}
--}

--		
-- Platform specific defaults
--

-- Default to dlopen version of OpenAL
--if not _OPTIONS["with-openal"] then
--	_OPTIONS["with-openal"] = "dlopen"
--end
--if _OPTIONS["with-openal"] then
--	_OPTIONS["with-openal"] = "openal-" .. _OPTIONS["with-openal"]
--end

-- main engine code
include "code/engine"

-- game mod code
include "base/code/game"
include "base/code/cgame"
include "base/code/ui"

-- tools
include "code/tools/xmap2"
include "code/tools/master"
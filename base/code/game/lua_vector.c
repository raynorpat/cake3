/*
===========================================================================
Copyright (C) 2006 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of XreaL source code.

XreaL source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

XreaL source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XreaL source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// lua_vector.c -- vector library for Lua


#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "g_local.h"

static int vector_New(lua_State * L)
{
	vec_t          *v;

	v = lua_newuserdata(L, sizeof(vec3_t));

	luaL_getmetatable(L, "vector");
	lua_setmetatable(L, -2);
	
	VectorClear(v);

	return 1;
}

static int vector_Construct(lua_State * L)
{
	vec_t          *v;

	v = lua_newuserdata(L, sizeof(vec3_t));

	luaL_getmetatable(L, "vector");
	lua_setmetatable(L, -2);
	
	v[0] = luaL_optnumber(L, 1, 0);
	v[1] = luaL_optnumber(L, 2, 0);
	v[2] = luaL_optnumber(L, 3, 0);

	return 1;
}

static int vector_Set(lua_State * L)
{
	vec_t          *v;

	v = lua_getvector(L, 1);
	
	v[0] = luaL_optnumber(L, 1, 0);
	v[1] = luaL_optnumber(L, 2, 0);
	v[2] = luaL_optnumber(L, 3, 0);

	return 1;
}

// *INDENT-OFF*
static int vector_Index(lua_State * L)
{
	vec_t          *v;
	const char     *i;
	
	v = lua_getvector(L, 1);
	i = luaL_checkstring(L, 2);
	
	switch (*i)
	{
		case '0': case 'x': case 'r': lua_pushnumber(L, v[0]); break;
		case '1': case 'y': case 'g': lua_pushnumber(L, v[1]); break;
		case '2': case 'z': case 'b': lua_pushnumber(L, v[2]); break;
		default: lua_pushnil(L); break;
	}
	
	return 1;
}

static int vector_NewIndex(lua_State * L)
{
	vec_t          *v;
	const char     *i;
	vec_t           t;
	
	v = lua_getvector(L, 1);
	i = luaL_checkstring(L, 2);
	t = luaL_checknumber(L, 3);
	
	switch (*i)
	{
		case '0': case 'x': case 'r': v[0] = t; break;
		case '1': case 'y': case 'g': v[1] = t; break;
		case '2': case 'z': case 'b': v[2] = t; break;
		default: break;
	}
	
	return 1;
}
// *INDENT-ON*

static int vector_GC(lua_State * L)
{
//	G_Printf("Lua says bye to vector = %p\n", lua_getvector(L));
	
	return 0;
}

static int vector_ToString(lua_State * L)
{
	vec_t          *vec;
	
	vec = lua_getvector(L, 1);
	lua_pushstring(L, va("(%i %i %i)", (int)vec[0], (int)vec[1], (int)vec[2]));
	
	return 1;
}

static const luaL_reg vector_ctor[] = {
	{"New", vector_New},
	{"Construct", vector_Construct},
	{"Set", vector_Set},
	{NULL, NULL}
};

static const luaL_reg vector_meta[] = {
	{"__index", vector_Index},
	{"__newindex", vector_NewIndex},
	{"__gc", vector_GC},
	{"__tostring", vector_ToString},
	{NULL, NULL}
};

int luaopen_vector(lua_State * L)
{
	luaL_newmetatable(L, "vector");
	
	luaL_register(L, NULL, vector_meta);
	luaL_register(L, "vector", vector_ctor);

	return 1;
}

void lua_pushvector(lua_State * L, vec3_t v)
{
	vec_t          *vec;

	vec = lua_newuserdata(L, sizeof(vec3_t));

	luaL_getmetatable(L, "vector");
	lua_setmetatable(L, -2);
	
	VectorCopy(v, vec);
}

vec_t *lua_getvector(lua_State * L, int argNum)
{
	void           *ud;

	ud = luaL_checkudata(L, argNum, "vector");
	luaL_argcheck(L, ud != NULL, argNum, "`vector' expected");
	return (vec_t *) ud;
}

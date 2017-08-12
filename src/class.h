#pragma once

#include <lua.h>
#include <lauxlib.h>

void new(lua_State *L, int argc);
void new_class(lua_State *L, int inherits);
void new_type(lua_State *L,
    int inherits, int create_idx,
    luaL_Reg funcs[], int nup);

#pragma once

#include <lua.h>

void mkmodule(lua_State *L, const char *name);

int lua_pcall_tb(lua_State *L, int argc, int retc);

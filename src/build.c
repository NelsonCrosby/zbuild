
#include <stdio.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua-export.h"
#include "util.h"


static void setup_path(lua_State *L)
{
    lua_getglobal(L, "package");
    // lua_getfield(L, -1, "path");
    lua_pushstring(L, LUA_SRCDIR "/?.lua;" LUA_SRCDIR "/?/init.lua");
    // lua_concat(L, 2);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
}


static void setup_libs(lua_State *L)
{
    setup_path(L);
    new_setup(L);
}


int main(int argc, char *argv[])
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    luaL_loadfile(L, LUA_SRCDIR "/main.lua");

    setup_libs(L);

    int status = lua_pcall_tb(L, 0, 1);
    if (status) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    for (int i = 0; i < argc; i += 1) {
        lua_pushstring(L, argv[i]);
    }
    status = lua_pcall_tb(L, argc, 1);

    if (status) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
    } else {
        status = lua_tointeger(L, -1);
    }

    lua_close(L);
    return status;
}

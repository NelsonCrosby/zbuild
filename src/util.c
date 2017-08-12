
#include <lua.h>
#include <lauxlib.h>

#include "util.h"

void mkmodule(lua_State *L, const char *name)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");

    lua_pushvalue(L, -3);
    lua_setfield(L, -2, name);

    lua_pop(L, 3);
}

static int pcall_msgh(lua_State *L)
{
    luaL_traceback(L, L, lua_tostring(L, -1), 0);
    return 1;
}

int lua_pcall_tb(lua_State *L, int argc, int retc)
{
    lua_pushcfunction(L, pcall_msgh);
    lua_insert(L, -(argc + 2));
    int status = lua_pcall(L, argc, retc, -(argc + 2));
    lua_remove(L, -(retc + 1));
    return status;
}

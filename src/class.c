
#include <stdio.h>

#include "class.h"


static int L_new_createtable(lua_State *L);


void new(lua_State *L, int argc)
{
    // Get creator/allocator
    lua_pushlightuserdata(L, L_new_createtable);
    lua_gettable(L, -(argc + 2));
    // Create data
    lua_call(L, 0, 1);
    // Setup type
    lua_pushvalue(L, -(argc + 2));
    lua_setmetatable(L, -2);
    // Copy self so it's top after init call
    lua_pushvalue(L, -1);
    lua_insert(L, -(argc + 2));
    // Initialise
    lua_getfield(L, -1, "init");
    if (!lua_isnil(L, -1)) {
        lua_insert(L, -(argc + 2));     // Put init function before args
        lua_insert(L, -(argc + 1));     // Put self as first arg
        lua_call(L, argc + 1, 0);
    } else {
        // Clear args, self, init
        lua_pop(L, argc + 2);
    }
    // Only leave self
    lua_remove(L, -2);
}

void new_class(lua_State *L, int inherits)
{
    new_type(L, inherits, 0, NULL, 0);
}

void new_type(lua_State *L,
    int inherits, int create_idx,
    luaL_Reg funcs[], int nup)
{
    // Create class
    lua_createtable(L, 0, 2);
    // Setup create function
    lua_pushlightuserdata(L, L_new_createtable);
    if (create_idx) lua_pushvalue(L, create_idx);
    else            lua_pushcfunction(L, L_new_createtable);
    lua_settable(L, -3);
    // Send table behind superclasses
    lua_insert(L, -(inherits + 1));

    // Iterate over all super-classes
    while (inherits-- > 0) {
        // Iterate over all keys in class
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            // Duplicate key, since it's needed for next
            lua_pushvalue(L, -2);
            lua_insert(L, -2);
            // Copy value to new class
            lua_settable(L, 1);
        }
        lua_pop(L, 1);
    }

    // Ensure index is set correctly
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    if (funcs) {
        luaL_setfuncs(L, funcs, nup);
    }
}


static int L_new_createtable(lua_State *L)
{
    lua_createtable(L, 0, 0);
    return 1;
}

static int L_new_create(lua_State *L)
{
    int argc = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    new(L, argc);
    return 1;
}

static int L_new(lua_State *L)
{
    if (lua_gettop(L) < 2) {
        luaL_error(L, "new: no class given");
        return 0;
    }

    lua_pushcclosure(L, L_new_create, 1);
    return 1;
}

static int L_new_class(lua_State *L)
{
    new_class(L, lua_gettop(L));
    return 1;
}

void new_setup(lua_State *L)
{
    lua_pushlightuserdata(L, (void *) L_new);
    // Setup new/mt
    lua_createtable(L, 0, 3);
    lua_pushcfunction(L, L_new_class);
    lua_setfield(L, -2, "class");
    lua_pushcfunction(L, L_new);
    lua_setfield(L, -2, "__call");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    // Assign new
    lua_setglobal(L, "new");
}

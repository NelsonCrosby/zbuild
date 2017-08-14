
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <yaml.h>

#include "class.h"
#include "lua-export.h"
#include "util.h"


static int L_yaml_load(lua_State *L);

static const luaL_Reg yaml_reg[] = {
    {"load", L_yaml_load},
    {NULL, NULL}
};

void setup_yaml(lua_State *L)
{
    luaL_newlib(L, yaml_reg);
    mkmodule(L, "yaml");
}


struct read_handler_state {
    lua_State *L;
    size_t off;
    int idx;
};
static int read_handler_luastr(void *p, unsigned char *buf,
    size_t bufsize, size_t *readsize);
static int read_handler_luafunc(void *p, unsigned char *buf,
    size_t bufsize, size_t *readsize);
static int parse_value(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event);

// function yaml.load(src)
static int L_yaml_load(lua_State *L)
{
    struct read_handler_state rh_state = { L, 0, 1 };
    yaml_read_handler_t *rh;
    if (lua_isstring(L, 1)) {
        rh = read_handler_luastr;
    } else if (lua_isfunction(L, 1)) {
        rh = read_handler_luafunc;
    } else {
        lua_getfield(L, 1, "read");
        if (lua_isfunction(L, -1)) {
            rh_state.idx = lua_gettop(L);
            rh_state.off = 1;
            rh = read_handler_luafunc;
        } else {
            return luaL_error(L, "yaml.load source must either be "
                                 "str, func, file, or object with read method");
        }
    }

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input(&parser, rh, (void *) &rh_state);

    int done = 0;
    int ndocs = 0;
    yaml_event_t event;
    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            lua_pushfstring(L, "yaml parse error: %s (%d:%d)", parser.problem,
                parser.problem_mark.line, parser.problem_mark.column);
            goto _err;
        }

        switch (event.type) {
        case YAML_STREAM_END_EVENT:
            done = 1;
        case YAML_NO_EVENT:
        case YAML_STREAM_START_EVENT:
            yaml_event_delete(&event);
            break;
        case YAML_DOCUMENT_START_EVENT:
            {
                ndocs += 1;
                yaml_event_delete(&event);
                if (!parse_value(L, &parser, &event))
                    goto _err;
                break;
            }
        }
    }

    yaml_parser_delete(&parser);
    return ndocs;

_err:
    yaml_parser_delete(&parser);
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}


static int parse_scalar(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event);
static int parse_mapping(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event);
static int parse_sequence(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event);

static int parse_value(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event)
{
    if (!yaml_parser_parse(parser, event)) {
        lua_pushfstring(L, "yaml parse error: %s (%d:%d)", parser->problem,
            parser->problem_mark.line, parser->problem_mark.column);
        return 0;
    }

    switch (event->type) {
    case YAML_MAPPING_END_EVENT:
    case YAML_SEQUENCE_END_EVENT:
        return -1;
    case YAML_SCALAR_EVENT:
        return parse_scalar(L, parser, event);
    case YAML_MAPPING_START_EVENT:
        return parse_mapping(L, parser, event);
    case YAML_SEQUENCE_START_EVENT:
        return parse_sequence(L, parser, event);
    default:
        lua_pushfstring(L, "unrecognised yaml event type %d", event->type);
        yaml_event_delete(event);
        return 0;
    }
}

static int parse_scalar(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event)
{
    lua_pushlstring(L, event->data.scalar.value,
                    event->data.scalar.length);
    yaml_event_delete(event);
    return 1;
}

static int parse_mapping(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_event_delete(event);
    lua_createtable(L, 0, 0);

    while (1) {
        int kstatus = parse_value(L, parser, event);
        if (!kstatus)
            return 0;
        if (kstatus == -1) {
            yaml_event_delete(event);
            break;
        }

        if (!parse_value(L, parser, event))
            return 0;
        
        lua_settable(L, -3);
    }

    return 1;
}

static int parse_sequence(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_event_delete(event);
    lua_createtable(L, 0, 0);
    lua_Integer i = 1;

    while (1) {
        lua_pushinteger(L, i);

        int kstatus = parse_value(L, parser, event);
        if (!kstatus) {
            lua_pop(L, 1);
            return 0;
        }
        if (kstatus == -1) {
            lua_pop(L, 1);
            yaml_event_delete(event);
            break;
        }

        lua_settable(L, -3);
        i += 1;
    }

    return 1;
}


static int read_handler_luastr(void *p, unsigned char *buf,
    size_t bufsize, size_t *readsize)
{
    struct read_handler_state *state = p;

    size_t len;
    const char *str = lua_tolstring(state->L, state->idx, &len);
    str += state->off;

    size_t remaining = len - state->off;
    size_t readlen = remaining < bufsize ? remaining : bufsize;
    memcpy(buf, str, readlen);
    state->off += readlen;

    *readsize = readlen;
    return 1;
}

static int read_handler_luafunc(void *p, unsigned char *buf,
    size_t bufsize, size_t *readsize)
{
    struct read_handler_state *state = p;
    lua_State *L = state->L;

    lua_pushvalue(L, state->idx);
    if (state->off)
        lua_pushvalue(L, state->off);
    lua_pushinteger(L, bufsize);
    int err = lua_pcall_tb(L, 1, 1);
    if (err) {
        *readsize = 0;
        return 0;
    }

    if (lua_isnil(L, -1)) {
        *readsize = 0;  // EOF
    } else {
        size_t len;
        const char *str = lua_tolstring(L, -1, &len);
        memcpy(buf, str, len);
        *readsize = len;
    }

    lua_pop(L, 1);
    return 1;
}

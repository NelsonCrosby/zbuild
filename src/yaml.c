
#include <math.h>
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

struct _str {
    size_t length;
    const char *value;
};
static int check_kw(size_t checklen, const char *checkval,
    const struct _str against[]);
static int check_integer(size_t checklen, const char *checkval,
    lua_Integer *out);
static int check_number(size_t checklen, const char *checkval,
    lua_Number *out);

static int parse_scalar(lua_State *L,
    yaml_parser_t *parser, yaml_event_t *event)
{
    size_t len = event->data.scalar.length;
    const char *str = event->data.scalar.value;

    yaml_scalar_style_t lit_style = event->data.scalar.style;
    if (lit_style != YAML_PLAIN_SCALAR_STYLE) {
        // Quoted, skip parsing
        lua_pushlstring(L, str, len);
        goto _ok;
    }

    static const struct _str check_null[] = {
        {0, ""}, {1, "~"}, {4, "null"}, {4, "Null"}, {4, "NULL"},
        {0, NULL}
    };
    if (check_kw(len, str, check_null)) {
        lua_pushnil(L);
        goto _ok;
    }

    static const struct _str check_true[] = {
        {1, "y"}, {1, "Y"}, {3, "yes"}, {3, "Yes"}, {3, "YES"},
        {4, "true"}, {4, "True"}, {4, "TRUE"},
        {2, "on"}, {2, "On"}, {2, "ON"},
        {0, NULL}
    };
    if (check_kw(len, str, check_true)) {
        lua_pushboolean(L, 1);
        goto _ok;
    }

    static const struct _str check_false[] = {
        {1, "n"}, {1, "N"}, {2, "no"}, {2, "No"}, {2, "NO"},
        {5, "false"}, {5, "False"}, {5, "FALSE"},
        {3, "off"}, {3, "Off"}, {3, "OFF"},
        {0, NULL}
    };
    if (check_kw(len, str, check_false)) {
        lua_pushboolean(L, 0);
        goto _ok;
    }
    
    lua_Integer n_i;
    if (check_integer(len, str, &n_i)) {
        lua_pushinteger(L, n_i);
        goto _ok;
    }

    lua_Number n_f;
    if (check_number(len, str, &n_f)) {
        lua_pushnumber(L, n_f);
        goto _ok;
    }

    // Isn't anything else, must be str
    lua_pushlstring(L, str, len);

_ok:
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

static int check_kw(size_t checklen, const char *checkval,
    const struct _str against[])
{
    for (int i = 0; against[i].value != NULL; i += 1) {
        if (checklen == against[i].length
            && strncmp(checkval, against[i].value, checklen) == 0) {
            return 1;
        }
    }

    return 0;
}

static int check_integer(size_t checklen, const char *checkval,
    lua_Integer *out)
{
    lua_Integer value = 0;
    int sign = 0;
    enum { FMT_PRE, FMT_SPEC, FMT_POST } format_pos = FMT_PRE;
    enum { FMT_DEC, FMT_BIN, FMT_OCT, FMT_HEX } format = FMT_DEC;

    for (int i = 0; i < checklen; i += 1) {
        char c = checkval[i];
        if (c == '-') {
            if (sign) return 0;
            sign = -1;
            continue;
        } else if (c == '+') {
            if (sign) return 0;
            sign = 1;
            continue;
        } else {
            sign = 1;
        }

        if (format_pos == FMT_PRE) {
            if (c == '0') {
                format = FMT_OCT;
                format_pos = FMT_SPEC;
                continue;
            } else {
                format_pos = FMT_POST;
            }
        } else if (format_pos == FMT_SPEC) {
            if (c == 'b') {
                format = FMT_BIN;
                format_pos = FMT_POST;
                continue;
            } else if (c == 'x') {
                format = FMT_HEX;
                format_pos = FMT_POST;
                continue;
            } else if (c >= '0' && c <= '7' || c == '_') {
                format_pos = FMT_POST;
            } else {
                return 0;
            }
        }

        if (format_pos == FMT_POST) {
            if (c == '_') continue;
            int cval = c - '0';
            switch (format) {
            case FMT_DEC:
                if (cval >= 0 && cval <= 9) {
                    value = (value * 10) + cval;
                    continue;
                } else break;
            case FMT_BIN:
                if (cval == 0 || cval == 1) {
                    value = (value << 1) | cval;
                    continue;
                } else break;
            case FMT_OCT:
                if (cval >= 0 && cval <= 7) {
                    value = (value << 3) | cval;
                    continue;
                } else break;
            case FMT_HEX:
                if (cval >= 0 && cval <= 9) {
                    value = (value << 4) | cval;
                    continue;
                } else if (c >= 'a' && c <= 'f') {
                    value = (value << 4) | ((c - 'a') + 10);
                    continue;
                } else if (c >= 'A' && c <= 'F') {
                    value = (value << 4) | ((c - 'A') + 10);
                    continue;
                } else break;
            }
        }

        return 0;
    }

    *out = value * sign;
    return 1;
}

static int check_number(size_t checklen, const char *checkval,
    lua_Number *out)
{
    lua_Number value = 0.0;
    lua_Number sign = 0.0;
    lua_Number fract_mag = 1.0;
    lua_Integer expn = 0;
    lua_Integer expn_sign = 0;
    enum { FMT_WHOLE, FMT_FRACT, FMT_EXP } fmt_pos = FMT_WHOLE;

    for (int i = 0; i < checklen; i += 1) {
        char c = checkval[i];
        if (!sign) {
            if (c == '+') {
                sign = 1.0;
                continue;
            } else if (c == '-') {
                sign = -1.0;
                continue;
            } else {
                sign = 1.0;
            }
        }
        
        if (fmt_pos == FMT_EXP && !expn_sign) {
            if (c == '+') {
                expn_sign = 1;
                continue;
            } else if (c == '-') {
                expn_sign = -1;
                continue;
            } else {
                expn_sign = 1;
            }
        }

        if (c == '.') {
            if (fmt_pos != FMT_WHOLE) return 0;
            fmt_pos = FMT_FRACT;
            continue;
        }

        if (c == 'e' || c == 'E') {
            if (fmt_pos == FMT_EXP) return 0;
            fmt_pos = FMT_EXP;
            expn = 0;
            continue;
        }

        if (c >= '0' && c <= '9') {
            int cval = c - '0';

            switch (fmt_pos) {
            case FMT_WHOLE:
                value = (value * 10.0) + ((lua_Number) cval);
                break;
            case FMT_FRACT:
                value += ((lua_Number) cval) * (fract_mag /= 10.0);
                break;
            case FMT_EXP:
                expn = (expn * 10) + cval;
                break;
            }
        }
    }

    *out = value * sign * l_mathop(pow)(10, (lua_Number) (expn * expn_sign));
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

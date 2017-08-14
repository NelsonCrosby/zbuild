
-include conf.make
-include devconf.make

CFLAGS += -MMD
LDLIBS += -llua -lyaml

build_SRC = build.c class.c util.c yaml.c
build_C = $(build_SRC:%=src/%)
build_O = $(build_C:.c=.o)
build_D = $(build_C:.c=.d)

CFLAGS += \
    -DPREFIX='"$(PREFIX)"' \
    -DLUA_SRCDIR='"$(LUA_SRCDIR)"'

all: build

clean:
	rm -v build $(build_O) $(build_D)

.PHONY: all clean

build: $(build_O)
	cc $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(build_D)

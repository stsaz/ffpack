# ffpack: deflate compression from zlib
# 2020, Simon Zolin

include ../config.mk

VER := 1.2.11
URL := https://www.zlib.net/zlib-$(VER).tar.xz
LIB := libz-ffpack.$(SO)

all: zlib-$(VER)
	make -rR $(LIB)

# download
zlib-$(VER).tar.xz:
	$(WGET) $(URL)

# unpack
zlib-$(VER): zlib-$(VER).tar.xz
	$(UNTAR_XZ) zlib-$(VER).tar.xz
	touch zlib-$(VER)

# build
SRC := adler32.c \
	deflate.c \
	infback.c \
	inffast.c \
	inflate.c \
	inftrees.c \
	trees.c \
	zutil.c
OBJ := zlib-ff.o $(SRC:.c=.o)

CFLAGS := -Izlib-$(VER) \
	-DZ_EXP -DNO_GZIP -DHAVE_UNISTD_H -DHAVE_STDARG_H -D_LARGEFILE64_SOURCE=1 \
	-O3 -flto -fvisibility=hidden -fno-asynchronous-unwind-tables -fpic
ifneq ($(OS),win)
	CFLAGS += -DHAVE_HIDDEN
endif

LDFLAGS := $(LINK_INSTALLNAME_LOADERPATH) -shared -s

zlib-ff.o: zlib-ff.c $(wildcard *.h)
	$(C) -I. $(CFLAGS) $< -o $@

%.o: zlib-$(VER)/%.c
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) $(LDFLAGS) $+ -o $@

clean:
	$(RM) $(OBJ) zlib-$(VER)

distclean: clean
	$(RM) libz-ffpack.so libz-ffpack.dll libz-ffpack.dylib

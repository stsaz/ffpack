# ffpack: deflate compression from zlib
# 2020, Simon Zolin

VER := 1.2.11
URL := https://www.zlib.net/zlib-$(VER).tar.xz

# set OS
ifndef $(OS)
	uname := $(shell uname)
	ifeq ($(uname),Linux)
		OS := linux
	else ifeq ($(uname),FreeBSD)
		OS := freebsd
	else ifeq ($(uname),Darwin)
		OS := apple
	else
		OS := win
	endif
endif

# set compiler
CPREFIX :=
# CPREFIX := x86_64-w64-mingw32-
COMPILER := gcc
ifeq ($(OS),freebsd)
	COMPILER := clang
endif
ifeq ($(OS),apple)
	COMPILER := clang
endif
C := $(CPREFIX)gcc -c
CXX := $(CPREFIX)g++ -c
LINKER := $(CPREFIX)gcc
ifeq ($(COMPILER),clang)
	C := clang -c
	CXX := clang++ -c
	LINKER := clang
endif
LD_INSTNAME_LDRPATH :=
ifeq ($(OS),apple)
	LD_INSTNAME_LDRPATH = -Wl,-install_name -Wl,@loader_path/$@
endif

# set utils
WGET := wget
RM := rm -rfv
CP := cp -u
SO := so
ifeq ($(OS),win)
	SO := dll
else ifeq ($(OS),apple)
	SO := dylib
endif
UNTAR_XZ := tar xJf

all: zlib-$(VER)
	make -rR libz-ff.$(SO)

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

LDFLAGS := $(LD_INSTNAME_LDRPATH) -shared -s

zlib-ff.o: zlib-ff.c $(wildcard *.h)
	$(C) -I. $(CFLAGS) $< -o$@

%.o: zlib-$(VER)/%.c
	$(C) $(CFLAGS) $< -o$@

libz-ff.$(SO): $(OBJ)
	$(LINKER) $(OBJ) $(LDFLAGS) -o$@

clean:
	$(RM) $(OBJ) zlib-$(VER)

distclean: clean
	$(RM) libz-ff.so libz-ff.dll libz-ff.dylib

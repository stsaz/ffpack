# zlib

include ../config.mk

VER := 1.3.2
URL := https://github.com/madler/zlib/releases/download/v$(VER)/zlib-$(VER).tar.xz
PKG_SHA256 := d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3
PKG := $(FFPACK)/zlib/$(notdir $(URL))
PKGDIR := zlib-$(VER)
LIB := libz-ffpack.$(SO)

default: $(PKGDIR)
	$(SUBMAKE) $(LIB)
	$(SUBMAKE) libz.a

# download
$(PKG):
	$(CURL) -o $@ $(URL)

# unpack
$(PKGDIR): $(PKG)
	echo "$(PKG_SHA256) *$(PKG)" | $(SHA256SUM) -c -
	$(UNTAR) $(PKG)
	touch $@

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

CFLAGS += -I$(PKGDIR) \
	-DZ_EXP -DNO_GZIP -DHAVE_UNISTD_H -DHAVE_STDARG_H -D_LARGEFILE64_SOURCE=1 \
	-O3 -flto -fvisibility=hidden -fno-asynchronous-unwind-tables
ifneq ($(OS),win)
	CFLAGS += -DHAVE_HIDDEN
endif

zlib-ff.o: $(FFPACK)/zlib/zlib-ff.c $(FFPACK)/zlib/zlib-ff.h
	$(C) $(CFLAGS) $< -o $@

%.o: $(PKGDIR)/%.c
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@

libz.a: $(OBJ) crc32.o
	$(AR) rcs $@ $+

clean:
	$(RM) $(OBJ) zlib-$(VER)

distclean: clean
	$(RM) libz-ffpack.so libz-ffpack.dll libz-ffpack.dylib

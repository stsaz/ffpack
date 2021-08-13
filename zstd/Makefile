# ffpack: libzstd
# 2021, Simon Zolin

VER := 1.5.0
URL := https://github.com/facebook/zstd/releases/download/v$(VER)/zstd-$(VER).tar.zst
PKG := ./zstd-$(VER).tar.zst
PKGDIR := ./zstd-$(VER)
LIBDIR := ./zstd-$(VER)/lib

include ../makeconf

all: $(LIBDIR)
	$(MAKE) -rR libzstd-ffpack.$(SO)

# download
$(PKG):
	$(WGET) $(URL)

# unpack
$(LIBDIR): $(PKG)
	$(UNTAR_ZST) $<
	touch $@

# build
OBJ := zstd-ff.o \
	$(LIBDIR)/common/debug.o \
	$(LIBDIR)/common/entropy_common.o \
	$(LIBDIR)/common/error_private.o \
	$(LIBDIR)/common/fse_decompress.o \
	$(LIBDIR)/common/pool.o \
	$(LIBDIR)/common/threading.o \
	$(LIBDIR)/common/xxhash.o \
	$(LIBDIR)/common/zstd_common.o \
	$(LIBDIR)/compress/fse_compress.o \
	$(LIBDIR)/compress/hist.o \
	$(LIBDIR)/compress/huf_compress.o \
	$(LIBDIR)/compress/zstd_compress.o \
	$(LIBDIR)/compress/zstd_compress_literals.o \
	$(LIBDIR)/compress/zstd_compress_sequences.o \
	$(LIBDIR)/compress/zstd_compress_superblock.o \
	$(LIBDIR)/compress/zstd_double_fast.o \
	$(LIBDIR)/compress/zstd_fast.o \
	$(LIBDIR)/compress/zstd_lazy.o \
	$(LIBDIR)/compress/zstd_ldm.o \
	$(LIBDIR)/compress/zstd_opt.o \
	$(LIBDIR)/compress/zstdmt_compress.o \
	$(LIBDIR)/decompress/huf_decompress.o \
	$(LIBDIR)/decompress/zstd_ddict.o \
	$(LIBDIR)/decompress/zstd_decompress.o \
	$(LIBDIR)/decompress/zstd_decompress_block.o \
	$(LIBDIR)/dictBuilder/cover.o \
	$(LIBDIR)/dictBuilder/divsufsort.o \
	$(LIBDIR)/dictBuilder/fastcover.o \
	$(LIBDIR)/dictBuilder/zdict.o

CFLAGS := -I$(LIBDIR) \
	-DXXH_NAMESPACE=ZSTD_ -DDEBUGLEVEL=0 -DZSTD_MULTITHREAD -DZSTDLIB_VISIBILITY= -DZDICTLIB_VISIBILITY= -DZSTDERRORLIB_VISIBILITY= \
	-O3 -flto -fno-asynchronous-unwind-tables -fvisibility=hidden -fpic

LDFLAGS := -shared -s $(LINK_INSTALLNAME_LOADERPATH)

zstd-ff.o: zstd-ff.c zstd-ff.h
	$(C) -I. $(CFLAGS) $< -o $@

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

libzstd-ffpack.$(SO): $(OBJ)
	$(LINK) $(LDFLAGS) $+ $(LINK_PTHREAD) -o $@


cleanobj:
	$(RM) $(OBJ)

cleantmp:
	$(RM) $(OBJ) $(PKGDIR)

clean: cleantmp
	$(RM) $(PKG) libzstd-ffpack.so libzstd-ffpack.dll libzstd-ffpack.dylib

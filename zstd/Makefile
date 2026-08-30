# ffpack: libzstd

include ../config.mk

VER := 1.5.7
URL := https://github.com/facebook/zstd/releases/download/v$(VER)/zstd-$(VER).tar.zst
PKG_SHA256 := 5b331d961d6989dc21bb03397fc7a2a4d86bc65a14adc5ffbbce050354e30fd2
PKG := $(FFPACK)/zstd/zstd-$(VER).tar.zst
PKGDIR := zstd-$(VER)
LIBDIR := zstd-$(VER)/lib
LIB := libzstd-ffpack.$(SO)

default: $(LIBDIR)
	$(SUBMAKE) $(LIB)

# download
$(PKG):
	$(CURL) -o $@ $(URL)

# unpack
$(LIBDIR): $(PKG)
	echo "$(PKG_SHA256) *$(PKG)" | $(SHA256SUM) -c -
	$(UNTAR) $<

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
	$(LIBDIR)/compress/zstd_preSplit.o \
	$(LIBDIR)/compress/zstdmt_compress.o \
	$(LIBDIR)/decompress/huf_decompress.o \
	$(LIBDIR)/decompress/zstd_ddict.o \
	$(LIBDIR)/decompress/zstd_decompress.o \
	$(LIBDIR)/decompress/zstd_decompress_block.o \
	$(LIBDIR)/dictBuilder/cover.o \
	$(LIBDIR)/dictBuilder/divsufsort.o \
	$(LIBDIR)/dictBuilder/fastcover.o \
	$(LIBDIR)/dictBuilder/zdict.o

ifeq "$(CPU)" "amd64"
	OBJ += $(LIBDIR)/decompress/huf_decompress_amd64.o
endif

CFLAGS += -I$(LIBDIR) \
	-DXXH_NAMESPACE=ZSTD_ -DDEBUGLEVEL=0 -DZSTD_MULTITHREAD -DZSTDLIB_VISIBILITY= -DZDICTLIB_VISIBILITY= -DZSTDERRORLIB_VISIBILITY= \
	-O3 -flto -fno-asynchronous-unwind-tables

zstd-ff.o: $(FFPACK)/zstd/zstd-ff.c $(FFPACK)/zstd/zstd-ff.h
	$(C) $(CFLAGS) $< -o $@

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

%.o: %.S
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) $(LINK_PTHREAD) -o $@


cleanobj:
	$(RM) $(OBJ)

cleantmp:
	$(RM) $(OBJ) $(PKGDIR)

clean: cleantmp
	$(RM) $(PKG) libzstd-ffpack.so libzstd-ffpack.dll libzstd-ffpack.dylib

# ffpack: lzma compression and filters from xz

include ../config.mk

VER := 5.8.3
URL := https://github.com/tukaani-project/xz/releases/download/v$(VER)/xz-$(VER).tar.xz
PKG_SHA256 := fff1ffcf2b0da84d308a14de513a1aa23d4e9aa3464d17e64b9714bfdd0bbfb6
PKG := $(FFPACK)/lzma/$(notdir $(URL))
PKGDIR := xz-$(VER)
LIB := liblzma-ffpack.$(SO)

default: $(PKGDIR)/config.h
	$(SUBMAKE) $(LIB)

# download
$(PKG):
	$(CURL) -o $@ $(URL)

# unpack, patch
$(PKGDIR): $(PKG)
	echo "$(PKG_SHA256) *$(PKG)" | $(SHA256SUM) -c -
	$(UNTAR) $(PKG)
	patch -d $(PKGDIR) -p1 <$(FFPACK)/lzma/0001-x86-filter.patch

# configure
CONFIGURE_ARGS :=
ifneq ($(CROSS_PREFIX),)
	CONFIGURE_ARGS += --host=x86_64-w64-mingw32
endif

$(PKGDIR)/config.h: | $(PKGDIR)
	cd $(PKGDIR) && ./configure $(CONFIGURE_ARGS) && \
		sed 's/#define HAVE_VISIBILITY 1/#define HAVE_VISIBILITY 0/' -i ./config.h

# build
SRC := \
	$(PKGDIR)/src/common/tuklib_cpucores.c \
	$(PKGDIR)/src/common/tuklib_physmem.c \
	\
	$(PKGDIR)/src/liblzma/check/check.c \
	$(PKGDIR)/src/liblzma/check/crc32_fast.c \
	$(PKGDIR)/src/liblzma/check/crc64_fast.c \
	$(PKGDIR)/src/liblzma/check/sha256.c \
	$(PKGDIR)/src/liblzma/common/alone_decoder.c \
	$(PKGDIR)/src/liblzma/common/alone_encoder.c \
	$(PKGDIR)/src/liblzma/common/auto_decoder.c \
	$(PKGDIR)/src/liblzma/common/block_buffer_encoder.c \
	$(PKGDIR)/src/liblzma/common/block_decoder.c \
	$(PKGDIR)/src/liblzma/common/block_encoder.c \
	$(PKGDIR)/src/liblzma/common/block_header_encoder.c \
	$(PKGDIR)/src/liblzma/common/block_util.c \
	$(PKGDIR)/src/liblzma/common/common.c \
	$(PKGDIR)/src/liblzma/common/easy_buffer_encoder.c \
	$(PKGDIR)/src/liblzma/common/easy_decoder_memusage.c \
	$(PKGDIR)/src/liblzma/common/easy_encoder.c \
	$(PKGDIR)/src/liblzma/common/easy_encoder_memusage.c \
	$(PKGDIR)/src/liblzma/common/easy_preset.c \
	$(PKGDIR)/src/liblzma/common/filter_buffer_decoder.c \
	$(PKGDIR)/src/liblzma/common/filter_buffer_encoder.c \
	$(PKGDIR)/src/liblzma/common/filter_common.c \
	$(PKGDIR)/src/liblzma/common/filter_decoder.c \
	$(PKGDIR)/src/liblzma/common/filter_encoder.c \
	$(PKGDIR)/src/liblzma/common/filter_flags_decoder.c \
	$(PKGDIR)/src/liblzma/common/filter_flags_encoder.c \
	$(PKGDIR)/src/liblzma/common/hardware_cputhreads.c \
	$(PKGDIR)/src/liblzma/common/hardware_physmem.c \
	$(PKGDIR)/src/liblzma/common/index.c \
	$(PKGDIR)/src/liblzma/common/index_decoder.c \
	$(PKGDIR)/src/liblzma/common/index_encoder.c \
	$(PKGDIR)/src/liblzma/common/index_hash.c \
	$(PKGDIR)/src/liblzma/common/lzip_decoder.c \
	$(PKGDIR)/src/liblzma/common/outqueue.c \
	$(PKGDIR)/src/liblzma/common/stream_buffer_encoder.c \
	$(PKGDIR)/src/liblzma/common/stream_encoder.c \
	$(PKGDIR)/src/liblzma/common/stream_flags_common.c \
	$(PKGDIR)/src/liblzma/common/stream_flags_encoder.c \
	$(PKGDIR)/src/liblzma/common/vli_decoder.c \
	$(PKGDIR)/src/liblzma/common/vli_encoder.c \
	$(PKGDIR)/src/liblzma/common/vli_size.c \
	$(PKGDIR)/src/liblzma/delta/delta_common.c \
	$(PKGDIR)/src/liblzma/delta/delta_decoder.c \
	$(PKGDIR)/src/liblzma/delta/delta_encoder.c \
	$(PKGDIR)/src/liblzma/lz/lz_decoder.c \
	$(PKGDIR)/src/liblzma/lz/lz_encoder.c \
	$(PKGDIR)/src/liblzma/lz/lz_encoder_mf.c \
	$(PKGDIR)/src/liblzma/lzma/fastpos_table.c \
	$(PKGDIR)/src/liblzma/lzma/lzma2_decoder.c \
	$(PKGDIR)/src/liblzma/lzma/lzma2_encoder.c \
	$(PKGDIR)/src/liblzma/lzma/lzma_decoder.c \
	$(PKGDIR)/src/liblzma/lzma/lzma_encoder.c \
	$(PKGDIR)/src/liblzma/lzma/lzma_encoder_optimum_fast.c \
	$(PKGDIR)/src/liblzma/lzma/lzma_encoder_optimum_normal.c \
	$(PKGDIR)/src/liblzma/lzma/lzma_encoder_presets.c \
	$(PKGDIR)/src/liblzma/rangecoder/price_table.c \
	$(PKGDIR)/src/liblzma/simple/arm.c \
	$(PKGDIR)/src/liblzma/simple/arm64.c \
	$(PKGDIR)/src/liblzma/simple/armthumb.c \
	$(PKGDIR)/src/liblzma/simple/ia64.c \
	$(PKGDIR)/src/liblzma/simple/powerpc.c \
	$(PKGDIR)/src/liblzma/simple/simple_coder.c \
	$(PKGDIR)/src/liblzma/simple/simple_decoder.c \
	$(PKGDIR)/src/liblzma/simple/simple_encoder.c \
	$(PKGDIR)/src/liblzma/simple/sparc.c \
	$(PKGDIR)/src/liblzma/simple/riscv.c \
	$(PKGDIR)/src/liblzma/simple/x86.c
OBJ := lzma-ff.o $(SRC:.c=.o)

CFLAGS += -I$(FFPACK)/lzma -I$(PKGDIR) \
	-I$(PKGDIR)/src/liblzma/ \
	-I$(PKGDIR)/src/liblzma/api \
	-I$(PKGDIR)/src/liblzma/check \
	-I$(PKGDIR)/src/liblzma/common \
	-I$(PKGDIR)/src/liblzma/delta \
	-I$(PKGDIR)/src/liblzma/lz \
	-I$(PKGDIR)/src/liblzma/lzma \
	-I$(PKGDIR)/src/liblzma/rangecoder \
	-I$(PKGDIR)/src/liblzma/simple \
	-I$(PKGDIR)/src/common \
	-DHAVE_CONFIG_H -DTUKLIB_SYMBOL_PREFIX=lzma_ \
	-O3 -flto -fno-asynchronous-unwind-tables -pthread

lzma-ff.o: $(FFPACK)/lzma/lzma-ff.c
	$(C) $(CFLAGS) $< -o $@

$(PKGDIR)/src/common/%.o: $(PKGDIR)/src/common/%.c
	$(C) $(CFLAGS) $< -o $@

HEADERS := $(wildcard $(PKGDIR)/src/liblzma/check/*.h) \
	$(wildcard $(PKGDIR)/src/liblzma/common/*.h) \
	$(wildcard $(PKGDIR)/src/liblzma/delta/*.h) \
	$(wildcard $(PKGDIR)/src/liblzma/lz/*.h) \
	$(wildcard $(PKGDIR)/src/liblzma/lzma/*.h) \
	$(wildcard $(PKGDIR)/src/liblzma/rangecoder/*.h) \
	$(wildcard $(PKGDIR)/src/liblzma/simple/*.h)

$(PKGDIR)/src/liblzma/%.o: $(PKGDIR)/src/liblzma/%.c $(HEADERS)
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $(LINKFLAGS) $+ -o $@

clean:
	$(RM) $(OBJ) $(PKGDIR)

distclean: clean
	$(RM) liblzma-ffpack.so liblzma-ffpack.dll liblzma-ffpack.dylib

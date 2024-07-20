include config.mk
export FFPACK := $(dir $(firstword $(MAKEFILE_LIST)))
LIBS := lzma zlib zstd

default: $(LIBS)
	$(SUBMAKE) md5check

.PHONY: lzma
lzma: liblzma-ffpack.$(SO)
liblzma-ffpack.$(SO):
	$(MAKE) -f $(FFPACK)/lzma/Makefile

.PHONY: zlib
zlib: libz-ffpack.$(SO)
libz-ffpack.$(SO):
	$(MAKE) -f $(FFPACK)/zlib/Makefile

.PHONY: zstd
zstd: libzstd-ffpack.$(SO)
libzstd-ffpack.$(SO):
	$(MAKE) -f $(FFPACK)/zstd/Makefile

md5:
	cd $(FFPACK) && md5sum -b \
		lzma/xz-5.2.4.tar.xz \
		zlib/zlib-1.2.11.tar.xz \
		zstd/zstd-1.5.0.tar.zst \
		>packages.md5

md5check:
	cd $(FFPACK) && md5sum -c packages.md5 --ignore-missing

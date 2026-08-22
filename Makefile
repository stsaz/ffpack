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

include config.mk
BINDIR := _$(SYS)-$(CPU)

LIBS := \
	lzma \
	zlib \
	zstd

default:
	mkdir -p $(BINDIR)
	$(MAKE) build

build: $(addprefix $(BINDIR)/lib,$(addsuffix -ffpack.$(SO),$(LIBS)))

libzstd: $(BINDIR)/libzstd-ffpack.$(SO)

$(BINDIR)/lib%-ffpack.$(SO): %
	$(MAKE) -I.. -C $<
	mkdir -p $(BINDIR)
	mv $</*.$(SO) $(BINDIR)/

md5:
	md5sum -b \
		lzma/xz-5.2.4.tar.xz \
		zlib/zlib-1.2.11.tar.xz \
		zstd/zstd-1.5.0.tar.zst \
		>packages.md5

md5check:
	md5sum -c packages.md5 --ignore-missing

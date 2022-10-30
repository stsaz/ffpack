include makeconf
# ARCH := CPU=i686
BINDIR := _$(OS)-amd64

libs:
	cd lzma && $(MAKE) -Rr
	cd zlib && $(MAKE) -Rr
	cd zstd && $(MAKE) -Rr

install:
	$(MKDIR) $(BINDIR)
	$(CP) \
		lzma/*.$(SO) \
		zlib/*.$(SO) \
		zstd/*.$(SO) \
		$(BINDIR)

md5:
	md5sum -b \
		lzma/xz-5.2.4.tar.xz \
		zlib/zlib-1.2.11.tar.xz \
		zstd/zstd-1.5.0.tar.zst \
		>packages.md5

md5check:
	md5sum -c packages.md5 --ignore-missing

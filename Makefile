all:
	echo "Use 'make libs' to build 3rd party dynamic libraries for compression"

libs:
	cd ../zlib && make -Rr
	cd ../lzma && make -Rr
	cd ../zstd && make -Rr

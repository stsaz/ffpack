# ffpack

ffpack is a fast C library that can pack (compress) and unpack (decompress) data to/from the popular archive formats.

ffpack code is header-only (`.h`-only) and doesn't need to be built into `.a/.so/.dll` before use - you just include `.h` file and that's all.
However, dependent libraries must be built into `.so/.dll`, because ffpack doesn't implement compression algorithms itself.

* Features
* How to use
* How to use the reader (single-file)
* How to use the writer (single-file)
* How to use the reader (multi-file)
* How to use the writer (multi-file)
* Test


## Features

| Purpose | Include | Dependencies |
| --- | --- | --- |
| .gz read/write | `ffpack/gz-read.h`, `ffpack/gz-write.h` | libz-ff |
| .xz read | `ffpack/xz-read.h` | liblzma-ff |
| .zip read/write | `ffpack/zip-read.h`, `ffpack/zip-write.h` | libz-ff |
| .7z read/write | `ffpack/7z-read.h` | liblzma-ff, libz-ff |
| .tar read/write | `ffpack/tar-read.h`, `ffpack/tar-write.h` |
| .iso read/write | `ffpack/iso-read.h`, `ffpack/iso-write.h` |
| lzma decompress | `lzma/lzma-ff.h` | |
| zlib compress/decompress | `zlib/zlib-ff.h` | |
| zstd compress/decompress | `zstd/zstd-ff.h` | |

Note: ffpack doesn't contain code that reads or writes files - this is the user's responsibility.

### Low-level functions

Use helper functions and structures if you want to write your own readers and writers.

* .gz format (`ffpack/base/gz.h`)
* .xz format (`ffpack/base/xz.h`)
* .zip format (`ffpack/base/zip.h`)
* .7z format (`ffpack/base/7z.h`)
* .tar format (`ffpack/base/tar.h`)
* .iso format (`ffpack/base/iso.h`)


## How to use

1. Clone repos:

	```sh
	git clone https://github.com/stsaz/ffbase
	git clone https://github.com/stsaz/ffpack
	```

2. Build dependent libraries.

	There are scripts to automatically download and build the required dependency libraries (based on these official packages: xz, zlib, zstd).

	```sh
	cd ffpack
	mkdir _linux-amd64
	cd _linux-amd64
	make -j8 -f ../Makefile -I ..
	```

	This command will create these files:

		liblzma-ff.so
		libz-ff.so
		libzstd-ffpack.so

	You may copy these files into your project directory.

3. In your build script:

	Compiler flags:

		-IFFBASE_DIR -IFFPACK_DIR

	Linker flags:

		-LLIBS_DIR -llzma-ff -lz-ff -lzstd-ffpack

	where `FFBASE_DIR` is your ffbase/ directory,
	`FFPACK_DIR` is your ffpack/ directory,
	and `LIBS_DIR` is the directory with compression libraries.

4. And then just use the necessary files:

		#include <ffpack/zip-read.h>


## How to use the reader (single-file)

```C
	ff... obj = {}
	ff..._config conf = {};
	conf.setting = value;
	ff..._open(&obj, &conf);

	ffstr input = {};
	for (;;) {
		ffstr output;
		int r = ff..._process(&obj, &input, &output);

		switch (r) {
		case FF..._DATA:
			// use data from 'output'
			continue;

		case FF..._DONE:
			goto done;

		case FF..._SEEK:
			seek(ff..._offset(&obj));
			// fallthrough

		case FF..._MORE:
			input = ...;
			continue;

		default:
			error(ff..._error(&obj));
		}
	}

	done:
	ff..._close(&obj);
```

## How to use the writer (single-file)

```C
	ff... obj = {}
	ff..._config conf = {};
	conf.setting = value;
	ff..._init(&obj, &conf);

	ffstr input = {};
	for (;;) {
		ffstr output;
		int r = ff..._process(&obj, &input, &output);

		switch (r) {
		case FF..._DATA:
			// use data from 'output'
			continue;

		case FF..._DONE:
			goto done;

		case FF..._MORE:
			if (have_more_data)
				input = ...;
			else
				ff..._finish(&obj);
			continue;

		default:
			error(ff..._error(&obj));
		}
	}

	done:
	ff..._destroy(&obj);
```

## How to use the reader (multi-file)

```C
	ff... reader = {}
	ff..._open(&reader);

	ffstr input = {};
	for (;;) {
		ffstr output;
		int r = ff..._process(&reader, &input, &output);

		switch (r) {
		case FF..._FILEINFO:
			const ff..._fileinfo_t *info = ff..._fileinfo(&reader);
			// info->hdr_offset
			// ...
			continue;

		case FF..._FILEHEADER:
			const ff..._fileinfo_t *info = ff..._fileinfo(&reader);
			// ...
			continue;

		case FF..._DATA:
			// use data from 'output'
			continue;

		case FF..._DONE:
		case FF..._FILEDONE:
			if (!need_more_files)
				goto done;
			ff..._fileread(&reader, file_offset);
			continue;

		case FF..._SEEK:
			seek(ff..._offset(&reader));
			// fallthrough

		case FF..._MORE:
			input = ...;
			continue;

		default:
			error(ff..._error(&reader));
		}
	}

	done:
	ff..._close(&reader);
```

## How to use the writer (multi-file)

```C
	ff... writer = {}
	int next_file = 1;

	ffstr input = {};
	for (;;) {

		if (next_file) {
			next_file = 0;
			ff..._config conf = {};
			conf.setting = value;
			if (0 != ff..._fileadd(&writer, &conf))
				error(ff..._error(&writer));
		}

		ffstr output;
		int r = ff..._process(&writer, &input, &output);

		switch (r) {
		case FF..._DATA:
			// use data from 'output'
			continue;

		case FF..._FILEDONE:
			if (have_more_files)
				next_file = 1;
			else
				ff..._finish(&writer);
			continue;

		case FF..._DONE:
			goto done;

		case FF..._MORE:
			if (have_more_data)
				input = ...;
			else
				ff..._filefinish(&writer);
			continue;

		default:
			error(ff..._error(&writer));
		}
	}

	done:
	ff..._destroy(&writer);
```

## Test

```sh
git clone https://github.com/stsaz/ffbase
git clone https://github.com/stsaz/ffpack
cd ffpack/test
make libs
make
./ffpack-test all
```

## License

ffpack is in the public domain.
Third-party code is the property of their owners.

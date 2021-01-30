# ffpack

ffpack is a fast C library that can pack (compress) and unpack (decompress) data to/from the popular archive formats.

ffpack code is header-only (`.h`-only) and doesn't need to be built into `.a/.so/.dll` before use - you just include `.h` file and that's all.
However, dependent libraries must be built into `.so/.dll`, because ffpack doesn't implement compression algorithms itself.

* Features
* Build dependency libraries
* Test
* How to use
* How to use the reader (single-file)
* How to use the writer (single-file)
* How to use the reader (multi-file)
* How to use the writer (multi-file)


## Features

* .gz read/write (`ffpack/gzread.h`, `ffpack/gzwrite.h`).  Dependencies: libz-ff.
* .xz read (`ffpack/xzread.h`).  Dependencies: liblzma-ff.
* .zip read/write (`ffpack/zipread.h`, `ffpack/zipwrite.h`).  Dependencies: libz-ff.
* .7z read/write (`ffpack/7zread.h`).  Dependencies: liblzma-ff, libz-ff.
* .tar read/write (`ffpack/tarread.h`, `ffpack/tarwrite.h`).
* .iso read/write (`ffpack/isoread.h`, `ffpack/isowrite.h`)

It doesn't contain code that reads or writes files - this is the responsibility of the user.

### Low-level functions

Use helper functions and structures if you want to write your own readers and writers.

* .gz format (`ffpack/gz-fmt.h`)
* .xz format (`ffpack/xz-fmt.h`)
* .zip format (`ffpack/zip-fmt.h`)
* .7z format (`ffpack/7z-fmt.h`)
* .tar format (`ffpack/tar-fmt.h`)
* .iso format (`ffpack/iso-fmt.h`)


## Build dependency libraries

There are scripts to automatically download and build the required dependency libraries (libz-ff, liblzma-ff from these official packages: zlib, xz).

	git clone https://github.com/stsaz/ffpack
	cd ffpack/test
	make depend

This command will create these files:

	lzma/liblzma-ff.so
	zlib/libz-ff.so

You should use them when compiling and linking your code with ffpack.


## How to use

1. Clone repos:

		$ git clone https://github.com/stsaz/ffbase
		$ git clone https://github.com/stsaz/ffpack

2. Build dependent libraries:

		cd ffpack/test
		make depend

3. In your build script:

		-IFFBASE_DIR -IFFPACK_DIR

where `FFBASE_DIR` is your ffbase/ directory,
and `FFPACK_DIR` is your ffpack/ directory.

4. And then just use the necessary files:

		#include <ffpack/zipread.h>


## How to use the reader (single-file)

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


## How to use the writer (single-file)

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


## How to use the reader (multi-file)

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


## How to use the writer (multi-file)

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


## Test

	git clone https://github.com/stsaz/ffbase
	git clone https://github.com/stsaz/ffpack
	cd ffpack/test
	make depend
	make
	./ffpack-test all


## License

Third-party code is the property of their owners.
All other code is absolutely free.

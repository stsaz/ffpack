# ffpack

ffpack is a fast C library that can pack (compress) and unpack (decompress) data.

* Features
* How to use the reader
* How to use the writer


## Features

* .gz read/write (`ffpack/gzread.h`, `ffpack/gzwrite.h`)
* .xz read (`ffpack/xzread.h`)

It doesn't contain code that reads or writes files - this is the responsibility of the user.

### Low-level functions

Use helper functions and structures if you want to write your own readers and writers.

* .gz format (`ffpack/gz-fmt.h`)
* .xz format (`ffpack/xz-fmt.h`)


## How to use the reader

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


## How to use the writer

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
			input = ...;
			continue;

		default:
			error(ff..._error(&obj));
		}
	}

	done:
	ff..._destroy(&obj);


## License

This code is absolutely free.

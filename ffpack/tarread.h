/** ffpack: .tar reader
2020, Simon Zolin */

/*
fftarread_open
fftarread_close
fftarread_process
fftarread_offset
fftarread_error
fftarread_fileinfo
*/

#pragma once

#include <ffpack/tar-fmt.h>
#include <ffpack/path.h>
#include <ffbase/vector.h>

typedef struct tar_fileinfo fftarread_fileinfo_t;

typedef struct fftarread {
	ffuint state, state_next;
	ffuint gather_size;
	const char *error;
	ffvec buf;
	ffstr name;
	ffuint64 offset; // current input offset
	ffuint64 size;
	fftarread_fileinfo_t fileinfo;
	int long_name;
} fftarread;

enum FFTARREAD_R {
	/* Need more input data
	Expecting fftarread_process() with more data */
	FFTARREAD_MORE,

	/* File info is ready - user may call fftarread_fileinfo()
	Expecting fftarread_process() */
	FFTARREAD_FILEHEADER,

	/* Have more output data for user
	Expecting fftarread_process() */
	FFTARREAD_DATA,

	/* Finished reading file data or the whole archive
	Expecting fftarread_process() or fftarread_close() */
	FFTARREAD_FILEDONE,

	/* Finished reading the archive
	Expecting fftarread_close() */
	FFTARREAD_DONE,

	FFTARREAD_WARNING,

	/* Fatal error */
	FFTARREAD_ERROR,
};

/** Get input offset */
static inline ffuint64 fftarread_offset(fftarread *t)
{
	return t->offset;
}

/** Get last error message */
static inline const char* fftarread_error(fftarread *t)
{
	return t->error;
}

/** Get file info */
static inline fftarread_fileinfo_t* fftarread_fileinfo(fftarread *t)
{
	return &t->fileinfo;
}

static inline int fftarread_open(fftarread *t)
{
	ffmem_zero_obj(t);
	if (NULL == ffvec_allocT(&t->buf, 4096, char)
		|| NULL == ffstr_alloc(&t->name, 4096)) {
		return -1;
	}
	return 0;
}

static inline void fftarread_close(fftarread *t)
{
	ffvec_free(&t->buf);
	ffstr_free(&t->name);
}

static inline int fftarread_process(fftarread *t, ffstr *input, ffstr *output)
{
	enum {
		R_INIT, R_HDR, R_HDR_CONT, R_LONGNAME, R_SKIP,
		R_DATA, R_PADDING, R_FDONE, R_FIN,
		R_GATHER,
	};
	ffssize r;
	ffstr data;

	for (;;) {
		switch (t->state) {

		case R_INIT:
			t->gather_size = 512;
			t->state = R_GATHER;  t->state_next = R_HDR;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&t->buf, &t->buf.cap, input->ptr, input->len, t->gather_size, &data);
			if (r < 0) {
				return FFTARREAD_ERROR;
			}
			ffstr_shift(input, r);
			t->offset += r;
			if (data.len == 0)
				return FFTARREAD_MORE;
			t->buf.len = 0;
			t->state = t->state_next;
			break;

		case R_HDR: {
			if (data.ptr[0] == '\0') {
				t->size = 2*512;
				t->state = R_FIN;
				continue;
			}

			char *namebuf = NULL;
			if (!t->long_name) {
				ffmem_zero_obj(&t->fileinfo);
				namebuf = t->name.ptr;
			}
			r = tar_hdr_read(data.ptr, &t->fileinfo, namebuf);
			if (r != 0) {
				if (r & TAR_ESIZE) {
					t->error = "invalid size number";
					return FFTARREAD_ERROR;
				}

				if (r & TAR_ENUMBER)
					t->error = "invalid number";
				else if (r & TAR_ECHECKSUM)
					t->error = "incorrect checksum";
				else if (r & TAR_EHAVEDATA)
					t->error = "directory or link entry must not have data";
				t->state = R_HDR_CONT;
				return FFTARREAD_WARNING;
			}
		}
			// fallthrough

		case R_HDR_CONT:
			switch (t->fileinfo.type) {
			case TAR_LONG:
				if (t->long_name) {
					t->error = "2 consequent long name headers";
					return FFTARREAD_ERROR;
				}
				if (t->fileinfo.size > 4096) {
					t->error = "too long name";
					return FFTARREAD_ERROR;
				}
				t->gather_size = ffint_align_ceil2(t->fileinfo.size, 512);
				t->state = R_GATHER;  t->state_next = R_LONGNAME;
				t->long_name = 1;
				continue;

			case TAR_EXTHDR:
			case TAR_NEXTHDR:
				t->gather_size = 512;
				t->state = R_GATHER;  t->state_next = R_SKIP;
				continue;
			}

			t->long_name = 0;
			t->size = t->fileinfo.size;
			t->state = R_DATA;
			if (t->fileinfo.size == 0)
				t->state = R_FDONE;

			if (1)
				t->fileinfo.name.len = _ffpack_path_normalize(t->fileinfo.name.ptr, t->fileinfo.name.len
					, t->fileinfo.name.ptr, t->fileinfo.name.len, FFPATH_SIMPLE);

			return FFTARREAD_FILEHEADER;

		case R_LONGNAME:
			ffstr_copy(&t->name, 4096, data.ptr, t->fileinfo.size);
			t->fileinfo.name = t->name;
			t->state = R_GATHER;  t->state_next = R_HDR;
			continue;

		case R_SKIP:
			t->size -= data.len;
			t->gather_size = 512;
			if (t->size == 0) {
				t->state = R_GATHER;  t->state_next = R_HDR;
			} else {
				t->state = R_GATHER;  t->state_next = R_SKIP;
			}
			continue;

		case R_DATA:
			if (input->len == 0)
				return FFTARREAD_MORE;
			ffstr_set(output, input->ptr, ffmin64(t->size, input->len));
			ffstr_shift(input, output->len);
			t->offset += output->len;
			t->size -= output->len;
			if (t->size == 0) {
				if ((t->fileinfo.size % 512) == 0) {
					t->state = R_FDONE;
				} else {
					t->gather_size = 512 - t->fileinfo.size % 512;
					t->state = R_GATHER;  t->state_next = R_PADDING;
				}
			}
			return FFTARREAD_DATA;

		case R_PADDING:
			ffstr_skipchar(&data, 0x00);
			if (data.len != 0) {
				t->error = "invalid padding data";
				return FFTARREAD_ERROR;
			}
			// fallthrough

		case R_FDONE:
			t->gather_size = 512;
			t->state = R_GATHER;  t->state_next = R_HDR;
			return FFTARREAD_FILEDONE;

		case R_FIN:
			r = data.len;
			ffstr_skipchar(&data, 0x00);
			if (data.len != 0) {
				t->error = "invalid padding data";
				return FFTARREAD_ERROR;
			}
			t->size -= r;
			if (t->size != 0) {
				t->gather_size = 512;
				t->state = R_GATHER;  t->state_next = R_FIN;
				continue;
			}
			return FFTARREAD_DONE;
		}
	}
}

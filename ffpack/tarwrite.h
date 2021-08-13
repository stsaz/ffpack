/** ffpack: .tar writer
2020, Simon Zolin */

/*
fftarwrite_init
fftarwrite_destroy
fftarwrite_fileadd
fftarwrite_filefinish
fftarwrite_process
fftarwrite_finish
fftarwrite_error
*/

#pragma once

#include <ffpack/tar-fmt.h>
#include <ffpack/path.h>

typedef struct fftarwrite {
	ffuint state;
	const char *error;
	ffvec buf;
	ffuint64 fsize, fsize_hdr;
	int file_fin, arc_fin;
} fftarwrite;

typedef struct tar_fileinfo fftarwrite_conf;

enum FFTARWRITE_R {
	/* Need more input data
	Expecting fftarwrite_process() with more data or fftarwrite_filefinish() */
	FFTARWRITE_MORE,

	/* Have more output data for user
	Expecting fftarwrite_process() */
	FFTARWRITE_DATA,

	/* Finished processing the current file
	Expecting fftarwrite_fileadd() or fftarwrite_finish() */
	FFTARWRITE_FILEDONE,

	/* Finished writing .tar file */
	FFTARWRITE_DONE,

	/* Fatal error */
	FFTARWRITE_ERROR,
};

static inline void fftarwrite_init(fftarwrite *w)
{
	ffmem_zero_obj(w);
}

/** Input data for the current file is finished */
static inline void fftarwrite_filefinish(fftarwrite *w)
{
	w->file_fin = 1;
}

/** All input data is finished */
static inline void fftarwrite_finish(fftarwrite *w)
{
	w->arc_fin = 1;
}

/** Get last error message */
static inline const char* fftarwrite_error(fftarwrite *w)
{
	return w->error;
}

static inline int fftarwrite_fileadd(fftarwrite *w, fftarwrite_conf *conf)
{
	int rc = -1;
	if (w->state != 0) // W_NEWFILE
		return -1;

	int dir = (conf->attr_unix & 0040000);

	ffstr name = {};
	if (NULL == ffstr_alloc(&name, conf->name.len + 1))
		return -1;
	name.len = _ffpack_path_normalize(name.ptr, conf->name.len, conf->name.ptr, conf->name.len, _FFPACK_PATH_FORCE_SLASH | _FFPACK_PATH_SIMPLE);
	if (name.len != 0 && dir) {
		if (name.ptr[name.len - 1] != '/')
			name.ptr[name.len++] = '/';
	}

	struct tar_fileinfo info = *conf;
	info.name = name;
	w->fsize_hdr = conf->size;

	int r;
	r = tar_hdr_write(NULL, &info);
	if (NULL == ffvec_reallocT(&w->buf, r, char))
		goto end;
	r = tar_hdr_write(w->buf.ptr, &info);
	if (r < 0) {
		w->error = "tar_filehdr_write";
		goto end;
	}
	w->buf.len = r;

	w->state = 1; // W_HDR
	rc = 0;

end:
	ffstr_free(&name);
	return rc;
}

static inline void fftarwrite_destroy(fftarwrite *w)
{
	ffvec_free(&w->buf);
}

static inline int fftarwrite_process(fftarwrite *w, ffstr *input, ffstr *output)
{
	enum {
		W_NEWFILE = 0, W_HDR = 1, W_DATA, W_PADDING, W_FDONE,
		W_FOOTER, W_DONE,
	};
	ffsize n;

	for (;;) {
		switch (w->state) {
		case W_NEWFILE:
			if (w->arc_fin) {
				w->state = W_FOOTER;
				continue;
			}
			w->error = "not ready";
			return FFTARWRITE_ERROR;

		case W_HDR:
			ffstr_set2(output, &w->buf);
			w->state = W_DATA;
			w->fsize = 0;
			return FFTARWRITE_DATA;

		case W_DATA:
			if (input->len == 0) {
				if (w->file_fin) {
					w->state = W_PADDING;
					continue;
				}
				return FFTARWRITE_MORE;
			}
			ffstr_set2(output, input);
			input->len = 0;
			w->fsize += output->len;
			return FFTARWRITE_DATA;

		case W_PADDING:
			n = w->fsize % 512;
			if (n != 0) {
				ffstr_set(output, w->buf.ptr, 512 - n);
				ffmem_zero(output->ptr, output->len);
				w->state = W_FDONE;
				return FFTARWRITE_DATA;
			}
			// fallthrough

		case W_FDONE:
			if (w->fsize != w->fsize_hdr) {
				w->error = "actual data size doesn't match the size in header";
				return FFTARWRITE_ERROR;
			}
			w->file_fin = 0;
			w->state = W_NEWFILE;
			return FFTARWRITE_FILEDONE;

		case W_FOOTER:
			if (NULL == ffvec_reallocT(&w->buf, 3*512, char))
				return FFTARWRITE_ERROR;
			ffstr_set(output, w->buf.ptr, 3*512);
			ffmem_zero(output->ptr, output->len);
			w->state = W_DONE;
			return FFTARWRITE_DATA;

		case W_DONE:
			return FFTARWRITE_DONE;

		default:
			FF_ASSERT(0);
			return FFTARWRITE_ERROR;
		}
	}
}

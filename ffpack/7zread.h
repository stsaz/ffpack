/** ffpack: .7z reader
2017,2021, Simon Zolin
*/

/*
ff7zread_error
ff7zread_open
ff7zread_close
ff7zread_process
ff7zread_nextfile
ff7zread_offset
*/

#pragma once

#include <ffpack/7z-fmt.h>
#include <zlib/zlib-ff.h>
#include <lzma/lzma-ff.h>
#include <ffbase/time.h>

struct z7_folder;
struct z7_filter;
struct z7_block;

typedef void (*ff7zread_log)(void *udata, ffuint level, ffstr msg);

typedef struct ff7zread {
	ffuint state;
	int err;
	ffuint64 off;
	ffuint hdr_packed :1;
	ffstr *input;
	ffvec buf;

	ffuint gstate;
	ffsize gsize;
	ffvec gbuf;
	ffstr gdata;

	struct z7_block *blks;
	ffuint iblk;

	ffvec folders; //z7_folder[] = (folder)... (folder for empty files)
	struct z7_folder *cur_folder;

	ffslice _filters; // struct z7_filter[]
	struct z7_filter *filters;
	ffuint ifilter;
	ffuint crc;

	ff7zread_log log;
	void *udata;
} ff7zread;

typedef struct z7_fileinfo ff7zread_fileinfo;

enum FF7ZREAD_R {
	FF7ZREAD_ERROR = 1,
	FF7ZREAD_DATA,
	FF7ZREAD_MORE,
	FF7ZREAD_SEEK,
	FF7ZREAD_FILEHEADER,
	FF7ZREAD_FILEDONE,
};

struct z7_filter {
	ffuint64 read, written;
	int err;
	ffuint init :1;
	ffuint fin :1;
	ffuint allow_fin :1;
	ffvec buf;
	ffstr in;
	union {
		struct {
			ffuint64 off;
			ffuint64 size;
			ff7zread *z;
		} input;
		lzma_decoder *lzma;
		z_ctx *zlib;
		struct {
			ffuint64 off;
			ffuint64 size;
		} bounds;
	};
	int (*process)(struct z7_filter *c);
	void (*destroy)(struct z7_filter *c);
};

static int z7_filter_init(struct z7_filter *c, const struct z7_coder *coder);

static inline const char* ff7zread_error(ff7zread *z)
{
	static const char* errs[] = {
		"", // Z7_EOK
		"system", // Z7_ESYS
		"bad signature", // Z7_EHDRSIGN
		"unsupported version", // Z7_EHDRVER
		"bad signature header CRC", // Z7_EHDRCRC
		"bad block ID", // Z7_EBADID
		"unknown block ID", // Z7_EUKNID
		"duplicate block", // Z7_EDUPBLOCK
		"no required block", // Z7_ENOREQ
		"unsupported", // Z7_EUNSUPP
		"unsupported coder method", // Z7_EUKNCODER
		"unsupported Folder flags", // Z7_EFOLDER_FLAGS
		"incomplete block", // Z7_EMORE
		"invalid blocks order", // Z7_EORDER
		"bad data", // Z7_EDATA
		"data checksum mismatch", // Z7_EDATACRC
		"liblzma error", // Z7_ELZMA
		"libz error", // Z7_EZLIB
	};
	ffuint e = z->err;
	if (e >= FF_COUNT(errs))
		return "";
	return errs[e];
}

#define _ERR(m, r) \
	(m)->err = (r),  FF7ZREAD_ERROR

enum {
	R_START, R_GATHER, R_GHDR, R_BLKID, R_META_UNPACK,
	R_FSTART, R_FDATA, R_FDONE, R_FNEXT,
};

#define ff7zread_offset(z)  ((z)->off)

static inline void ff7zread_open(ff7zread *z)
{
	(void)z;
}

static inline void _ff7zread_log(ff7zread *z, ffuint level, const char *fmt, ...)
{
	(void)level;
	if (z->log == NULL)
		return;

	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	z->log(z->udata, 0, s);
	ffstr_free(&s);
}

static int _ff7zr_deflate_init(struct z7_filter *c, ffuint method);
static int _ff7zr_lzma_init(struct z7_filter *c, ffuint method, const void *props, ffuint nprops);
static int _ff7zr_bounds_process(struct z7_filter *c);

static int z7_filter_init(struct z7_filter *c, const struct z7_coder *coder)
{
	int r;
	switch (coder->method) {

	case Z7_M_DEFLATE:
		if (0 != (r = _ff7zr_deflate_init(c, coder->method)))
			return r;
		break;

	case Z7_M_X86:
	case Z7_M_LZMA1:
	case Z7_M_LZMA2:
		if (0 != (r = _ff7zr_lzma_init(c, coder->method, coder->props, coder->nprops)))
			return r;
		break;

	default:
		return Z7_EUKNCODER;
	}

	c->init = 1;
	return 0;
}

enum {
	_FF7ZR_FILT_MORE,
	_FF7ZR_FILT_DATA,
	_FF7ZR_FILT_SEEK,
	_FF7ZR_FILT_ERR,
	_FF7ZR_FILT_DONE,
};

/** Take input data from user and pass to the next filter
Return seek request if needed */
static int z7_input_process(struct z7_filter *c)
{
	ff7zread *z = c->input.z;

	if (c->input.size == 0)
		return _FF7ZR_FILT_DONE;

	if (z->off != c->input.off) {
		z->off = c->input.off;
		return _FF7ZR_FILT_SEEK;
	}
	if (z->input->len == 0)
		return _FF7ZR_FILT_MORE;

	ffsize n = ffmin(c->input.size, z->input->len);
	ffstr_set(&c->buf, z->input->ptr, n);
	ffstr_shift(z->input, n);
	z->off += n;
	c->input.off += n;
	c->input.size -= n;
	return _FF7ZR_FILT_DATA;
}

static int _ff7zread_filters_create(ff7zread *z, struct z7_folder *fo)
{
	int r;
	ffuint i, k = 0;

	if (NULL == ffslice_zallocT(&z->_filters, 1 + Z7_MAX_CODERS + 1, struct z7_filter))
		return Z7_ESYS;
	z->filters = (struct z7_filter*)z->_filters.ptr;

	for (i = 0;  i != fo->coders;  i++) {
		struct z7_coder *cod = &fo->coder[i];
		struct z7_filter *fi;
		if (cod->stream.off != 0 || cod->stream.pack_size != 0) {
			if (k != 0)
				return Z7_EUNSUPP;
			fi = &z->filters[k++];
			if (cod->input_coders[0] != 0)
				return Z7_EUNSUPP;
			fi->input.off = cod->stream.off;
			fi->input.size = cod->stream.pack_size;
			fi->input.z = z;
			fi->process = z7_input_process;

		} else if (cod->input_coders[0] == 0
			|| (int)cod->input_coders[0] - 1 != (int)i - 1
			|| cod->input_coders[1] != 0) {

			/* Currently only a simple coder chain is supported:
			input -> unpack -> x86 -> bounds
			unpack must have an assigned input stream
			x86's input must be unpack's output
			*/
			return Z7_EUNSUPP;
		}

		if (cod->method == Z7_M_STORE)
			continue;
		fi = &z->filters[k++];
		if (0 != (r = z7_filter_init(fi, cod)))
			return r;
	}

	z->filters[k].process = _ff7zr_bounds_process;

	z->ifilter = 0;
	z->_filters.len = k + 1;
	return 0;
}

static void _ff7zread_filters_close(ff7zread *z)
{
	struct z7_filter *f;
	FFSLICE_WALK(&z->_filters, f) {
		if (f->init)
			f->destroy(f);
		ffvec_free(&f->buf);
	}
	ffslice_free(&z->_filters);
	z->filters = NULL;
}

static void z7_folders_free(ffvec *folders)
{
	struct z7_folder *fo;
	FFSLICE_WALK(folders, fo) {
		ff7zread_fileinfo *f;
		FFSLICE_WALK(&fo->files, f) {
			ffstr_free(&f->name);
		}
		ffvec_free(&fo->files);
		ffvec_free(&fo->empty);
	}
	ffvec_free(folders);
}

static inline void ff7zread_close(ff7zread *z)
{
	ffvec_free(&z->buf);
	ffvec_free(&z->gbuf);
	_ff7zread_filters_close(z);
	z7_folders_free(&z->folders);
	ffmem_free(z->blks);  z->blks = NULL;
}


static int _ff7zr_deflate_process(struct z7_filter *c);
static void _ff7zr_deflate_destroy(struct z7_filter *c);

static int _ff7zr_deflate_init(struct z7_filter *c, ffuint method)
{
	(void)method;
	int r;
	z_conf conf = {};
	if (0 != (r = z_inflate_init(&c->zlib, &conf)))
		return Z7_EZLIB;

	if (NULL == ffvec_alloc(&c->buf, 64 * 1024, 1)) {
		z_inflate_free(c->zlib);
		return Z7_ESYS;
	}

	c->process = _ff7zr_deflate_process;
	c->destroy = _ff7zr_deflate_destroy;
	return 0;
}

static void _ff7zr_deflate_destroy(struct z7_filter *c)
{
	z_inflate_free(c->zlib);  c->zlib = NULL;
}

static int _ff7zr_deflate_process(struct z7_filter *c)
{
	int r;
	ffsize n = c->in.len;
	r = z_inflate(c->zlib, c->in.ptr, &n, ffslice_end(&c->buf, 1), ffvec_unused(&c->buf), 0);
	if (r == Z_DONE)
		return _FF7ZR_FILT_DONE;
	if (r < 0) {
		c->err = Z7_EZLIB;
		return _FF7ZR_FILT_ERR;
	}

	ffstr_shift(&c->in, n);
	if (r == 0)
		return _FF7ZR_FILT_MORE;

	c->buf.len += r;
	return _FF7ZR_FILT_DATA;
}


static int _ff7zr_lzma_process(struct z7_filter *c);
static void _ff7zr_lzma_destroy(struct z7_filter *c);

static int _ff7zr_lzma_init(struct z7_filter *c, ffuint method, const void *props, ffuint nprops)
{
	int r;
	lzma_filter_props fp;

	switch (method) {
	case Z7_M_X86:
		fp.id = LZMA_FILT_X86;
		c->allow_fin = 1;
		break;
	case Z7_M_LZMA1:
		fp.id = LZMA_FILT_LZMA1;
		break;
	case Z7_M_LZMA2:
		fp.id = LZMA_FILT_LZMA2;
		break;
	}

	fp.props = (void*)props;
	fp.prop_len = nprops;
	if (0 != (r = lzma_decode_init(&c->lzma, 0, &fp, 1)))
		return Z7_ELZMA;

	if (NULL == ffvec_alloc(&c->buf, lzma_decode_bufsize(c->lzma, 64 * 1024), 1)) {
		lzma_decode_free(c->lzma);
		return Z7_ESYS;
	}

	c->process = _ff7zr_lzma_process;
	c->destroy = _ff7zr_lzma_destroy;
	return 0;
}

static void _ff7zr_lzma_destroy(struct z7_filter *c)
{
	lzma_decode_free(c->lzma);  c->lzma = NULL;
}

static int _ff7zr_lzma_process(struct z7_filter *c)
{
	int r;
	ffsize n = c->in.len;
	if (c->fin && n == 0 && c->allow_fin)
		n = (ffsize)-1;
	r = lzma_decode(c->lzma, c->in.ptr, &n, ffslice_end(&c->buf, 1), ffvec_unused(&c->buf));
	if (r == LZMA_DONE)
		return _FF7ZR_FILT_DONE;
	if (r < 0) {
		c->err = Z7_ELZMA;
		return _FF7ZR_FILT_ERR;
	}

	ffstr_shift(&c->in, n);
	if (r == 0)
		return _FF7ZR_FILT_MORE;

	c->buf.len += r;
	return _FF7ZR_FILT_DATA;
}


/** Cut data by absolute boundaries
Return the number of bytes processed from the beginning

....DATA....
ds  o   o+s de
*/
static ffsize _ff7zr_ffstr_cut_abs(ffstr *data, ffuint64 start, ffuint64 off, ffuint64 size)
{
	ffuint64 l, r, end = start + data->len;
	l = ffmin(ffmax(start, off), end);
	r = ffmin(off + size, end);
	data->ptr += l - start;
	data->len = r - l;
	return r - start;
}

static int _ff7zr_bounds_process(struct z7_filter *c)
{
	ffstr s = c->in;
	ffsize n = _ff7zr_ffstr_cut_abs(&s, c->read, c->bounds.off, c->bounds.size);
	ffstr_shift(&c->in, n);
	if (s.len == 0) {
		if (c->read + n == c->bounds.off + c->bounds.size)
			return _FF7ZR_FILT_DONE;
		return _FF7ZR_FILT_MORE;
	}
	ffstr_set2(&c->buf, &s);
	return _FF7ZR_FILT_DATA;
}

static int _ff7zread_filters_call(ff7zread *z, ffstr *output)
{
	int r;
	const ff7zread_fileinfo *f = &((ff7zread_fileinfo*)z->cur_folder->files.ptr)[z->cur_folder->ifile - 1];
	struct z7_filter *c, *next;
	ffsize inlen;

	c = &z->filters[z->ifilter];
	inlen = c->in.len;
	(void)inlen;
	r = c->process(c);
	c->read += inlen - c->in.len;

	switch (r) {

	case _FF7ZR_FILT_MORE:
		if (c->fin)
			return _ERR(z, Z7_EDATA);

		if (z->ifilter == 0)
			return FF7ZREAD_MORE;

		z->ifilter--;
		break;

	case _FF7ZR_FILT_DATA:
		c->written += c->buf.len;
		_ff7zread_log(z, 0, "filter#%u: %xL->%xL [%xU->%xU]"
			, z->ifilter, inlen, c->buf.len, c->read, c->written);

		if (z->ifilter + 1 == z->_filters.len) {
			ffstr_set2(output, &c->buf);
			z->crc = crc32((void*)output->ptr, output->len, z->crc);
			return FF7ZREAD_DATA;
		}

		next = c + 1;
		ffstr_set2(&next->in, &c->buf);
		c->buf.len = 0;
		z->ifilter++;
		break;

	case _FF7ZR_FILT_DONE:
		_ff7zread_log(z, 0, "filter#%u: done", z->ifilter);
		if (z->ifilter + 1 == z->_filters.len) {
			if (f->crc != z->crc) {
				_ff7zread_log(z, 0, "CRC mismatch: should be: %xu  computed: %xu", f->crc, z->crc);
				return _ERR(z, Z7_EDATACRC);
			}
			return FF7ZREAD_FILEDONE;
		}

		next = c + 1;
		next->fin = 1;
		z->ifilter++;
		break;

	case _FF7ZR_FILT_SEEK:
		if (z->ifilter != 0)
			return _ERR(z, 0);
		return FF7ZREAD_SEEK;

	case _FF7ZR_FILT_ERR:
		return _ERR(z, c->err);
	}

	return 0;
}

static int _ff7zread_prep_unpack_hdr(ff7zread *z)
{
	int r;
	struct z7_folder *fo = (struct z7_folder*)z->folders.ptr;

	if (z->hdr_packed)
		return Z7_EDATA; // one more packed header

	if (fo->files.len != 0)
		return Z7_EDATA; // files inside header
	if (NULL == ffvec_allocT(&fo->files, 1, ff7zread_fileinfo))
		return Z7_ESYS;
	ffmem_zero(fo->files.ptr, sizeof(ff7zread_fileinfo));
	fo->files.len = 1;
	ff7zread_fileinfo *f = (ff7zread_fileinfo*)fo->files.ptr;
	f->size = fo->unpack_size;
	f->crc = fo->crc;
	fo->ifile = 1;

	if (NULL == ffvec_realloc(&z->buf, fo->unpack_size, 1))
		return Z7_ESYS;
	if (0 != (r = _ff7zread_filters_create(z, fo)))
		return r;
	z->filters[z->_filters.len - 1].bounds.size = f->size;
	z->hdr_packed = 1;
	z->cur_folder = fo;
	return 0;
}

static int _ff7zread_prep_unpack_file(ff7zread *z)
{
	int r;
	const ff7zread_fileinfo *f = (ff7zread_fileinfo*)z->cur_folder->files.ptr;
	f = &f[z->cur_folder->ifile - 1];
	_ff7zread_log(z, 0, "unpacking file '%S'  size:%xU  offset:%xU  CRC:%xu"
		, &f->name, f->size, f->off, f->crc);

	if (z->cur_folder->coder[0].stream.off == 0) {
		z->state = R_FDONE;
		return 0;
	}
	z->state = R_FDATA;

	z->crc = 0;
	if (z->_filters.len == 0) {
		if (0 != (r = _ff7zread_filters_create(z, z->cur_folder)))
			return _ERR(z, r);
		z->filters[z->_filters.len - 1].bounds.off = f->off;
		z->filters[z->_filters.len - 1].bounds.size = f->size;
		z->off = z->cur_folder->coder[0].stream.off;
		return FF7ZREAD_SEEK;
	}

	z->filters[z->_filters.len - 1].bounds.off = f->off;
	z->filters[z->_filters.len - 1].bounds.size = f->size;
	return 0;
}

static int _ff7zread_blk_proc(ff7zread *z, const struct z7_binblock *blk)
{
	int r;
	ffuint id = blk->flags & 0xff;
	FF_ASSERT(z->iblk + 1 != Z7_MAX_BLOCK_DEPTH);
	z->blks[++z->iblk].id = id;

	ffstr d;
	ffstr_set2(&d, &z->gdata);
	if (blk->flags & Z7_F_SIZE) {
		ffuint64 size;
		if (0 == (r = z7_readint(&z->gdata, &size)))
			return Z7_EMORE;
		_ff7zread_log(z, 0, " [block size:%xU]", size);
		if (z->gdata.len < size)
			return Z7_EMORE;
		ffstr_set(&d, z->gdata.ptr, size);
	}

	int (*proc)(ffvec *folders, ffstr *d);

	if (blk->flags & Z7_F_CHILDREN) {
		z->blks[z->iblk].children = blk->data;

		if (z->blks[z->iblk].children[0].flags & Z7_F_SELF) {
			proc = z->blks[z->iblk].children[0].data;
			if (0 != (r = proc(&z->folders, &d)))
				return r;
		}

	} else {
		proc = blk->data;
		if (0 != (r = proc(&z->folders, &d)))
			return r;

		ffmem_zero_obj(&z->blks[z->iblk]);
		z->iblk--;
	}

	z->gdata.len = ffstr_end(&z->gdata) - d.ptr;
	z->gdata.ptr = d.ptr;
	return 0;
}

/**
Return enum FF7ZREAD_R */
/* .7z reading:
. Parse header
. Seek to meta block;  parse it:
 . Build packed streams list
 . Build file meta list, associate with packed streams
. If packed meta, seek to it;  unpack;  parse
. Wait for user command to unpack a file
. Unpack file data
*/
static inline int ff7zread_process(ff7zread *z, ffstr *input, ffstr *output)
{
	int r;
	z->input = input;
	_ff7z_log_param = z;

	for (;;) {
		switch (z->state) {

		case R_START:
			if (NULL == (z->blks = (struct z7_block*)ffmem_calloc(Z7_MAX_BLOCK_DEPTH, sizeof(struct z7_block))))
				return _ERR(z, Z7_ESYS);
			z->blks[0].children = z7_ctx_top;
			z->gsize = sizeof(struct z7_ghdr);
			z->state = R_GATHER,  z->gstate = R_GHDR;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&z->gbuf, &z->gbuf.cap, input->ptr, input->len, z->gsize, &z->gdata);
			if (r < 0)
				return _ERR(z, Z7_ESYS);
			ffstr_shift(input, r);
			z->off += r;
			if (z->gdata.len == 0)
				return FF7ZREAD_MORE;
			z->gbuf.len = 0;
			z->state = z->gstate;
			z->gsize = 0;
			continue;

		case R_GHDR: {
			struct z7_info info;
			if (0 != (r = z7_ghdr_read(&info, z->gdata.ptr)))
				return _ERR(z, r);
			z->gsize = info.hdr_size;
			z->state = R_GATHER,  z->gstate = R_BLKID;
			z->off = info.hdr_off;
			return FF7ZREAD_SEEK;
		}

		case R_BLKID: {
			if (z->gdata.len == 0) {
				if (z->iblk != 0)
					return _ERR(z, Z7_EDATA);
				z->cur_folder = (struct z7_folder*)z->folders.ptr;
				return FF7ZREAD_FILEHEADER;
			}
			ffuint64 blk_id;
			if (1 != z7_varint(z->gdata.ptr, 1, &blk_id))
				return _ERR(z, Z7_EBADID);
			ffstr_shift(&z->gdata, 1);

			_ff7zread_log(z, 0, "%*c[block:%xu  offset:%xU]"
				, (ffsize)z->iblk, ' ', blk_id, z->off);

			const struct z7_binblock *blk;
			if (0 != (r = z7_find_block(blk_id, &blk, &z->blks[z->iblk])))
				return _ERR(z, r);

			if (blk_id == Z7_T_End) {

				if (0 != (r = z7_check_req(&z->blks[z->iblk])))
					return _ERR(z, r);

				ffuint done_id = z->blks[z->iblk].id;
				ffmem_zero_obj(&z->blks[z->iblk]);
				z->iblk--;

				if (done_id == Z7_T_EncodedHeader) {
					if (0 != (r = _ff7zread_prep_unpack_hdr(z)))
						return _ERR(z, r);
					z->state = R_META_UNPACK;
				}
				continue;
			}

			if (0 != (r = _ff7zread_blk_proc(z, blk)))
				return _ERR(z, r);
			continue;
		}

		case R_META_UNPACK:
			if (0 == (r = _ff7zread_filters_call(z, output)))
				continue;

			if (r == FF7ZREAD_FILEDONE) {
				_ff7zread_filters_close(z);
				z7_folders_free(&z->folders);
				z->cur_folder = NULL;

				ffstr_set2(&z->gdata, &z->buf);
				z->buf.len = 0;
				z->state = R_BLKID;
				continue;

			} else if (r == FF7ZREAD_DATA) {
				ffvec_add(&z->buf, output->ptr, output->len, 1);
				output->len = 0;
				continue;
			}

			return r;

		case R_FSTART:
			if (0 != (r = _ff7zread_prep_unpack_file(z)))
				return r;
			continue;

		case R_FDATA:
			if (0 == (r = _ff7zread_filters_call(z, output)))
				continue;

			if (r == FF7ZREAD_FILEDONE)
				z->state = R_FNEXT;
			return r;

		case R_FDONE:
			z->state = R_FNEXT;
			return FF7ZREAD_FILEDONE;

		case R_FNEXT:
			return FF7ZREAD_FILEHEADER;

		}
	}
	return 0;
}

/**
Return NULL if no next file */
static inline const ff7zread_fileinfo* ff7zread_nextfile(ff7zread *z)
{
	if (z->cur_folder == NULL)
		return NULL;

	if (z->cur_folder->ifile == z->cur_folder->files.len) {
		if (z->cur_folder == ffslice_lastT(&z->folders, struct z7_folder))
			return NULL;
		_ff7zread_filters_close(z);
		z->cur_folder->ifile = 0;
		z->cur_folder++;
	}

	const ff7zread_fileinfo *f = (ff7zread_fileinfo*)z->cur_folder->files.ptr;
	z->state = R_FSTART;
	return &f[z->cur_folder->ifile++];
}

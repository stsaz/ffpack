/** ffpack: .iso reader (ISO 9660 + Joliet, Rock Ridge)
2017,2021, Simon Zolin
*/

/*
ffisoread_open
ffisoread_close
ffisoread_process
ffisoread_fileinfo
ffiso_file_isdir
ffisoread_offset
ffisoread_error
ffisoread_nextfile
ffisoread_storefile
ffisoread_readfile
*/

#pragma once

#include <ffpack/iso-fmt.h>
#include <ffbase/vector.h>
#include <ffbase/list.h>

typedef struct iso_file ffisoread_fileinfo_t;
typedef void (*ffisoread_log)(void *udata, ffuint level, ffstr msg);

#define ffiso_file_isdir(f)  ((f)->attr & ISO_FILE_DIR)

typedef struct ffisoread {
	ffuint state;
	ffuint nextstate;
	ffuint err; //enum ISO_E
	ffuint64 fsize;
	ffuint64 root_start;
	ffisoread_fileinfo_t curfile;
	fflist files;
	ffchain_item *fcursor;
	ffisoread_fileinfo_t *curdir;
	ffvec buf;
	ffstr d;
	ffvec fn;
	ffvec fullfn;

	ffisoread_log log;
	void *udata;

	ffuint64 inoff;
	ffuint options; // enum FFISOREAD_OPT
	ffuint joliet :1;
} ffisoread;

enum FFISOREAD_OPT {
	FFISOREAD_NO_JOLIET = 1, // don't parse Joliet extensions
	FFISOREAD_NO_RR = 2, // don't parse RR extensions
};

enum FFISOREAD_R {
	FFISOREAD_MORE,
	FFISOREAD_ERROR,
	FFISOREAD_SEEK,
	FFISOREAD_HDR, // header is read
	FFISOREAD_FILEMETA, // call ffisoread_fileinfo() to get file meta
	FFISOREAD_LISTEND, // reached the end of meta data
	FFISOREAD_DATA,
	FFISOREAD_FILEDONE, // file data is finished
};

/** Get current file info */
static inline ffisoread_fileinfo_t* ffisoread_fileinfo(ffisoread *o)
{
	return &o->curfile;
}

#define ffisoread_offset(o)  ((o)->inoff)

static inline const char* ffisoread_error(ffisoread *o)
{
	static const char* const errs[] = {
		"unsupported logical block size", // ISO_ELOGBLK
		"primary volume: bad id", // ISO_EPRIMID
		"primary volume: bad version", // ISO_EPRIMVER
		"unsupported feature", // ISO_EUNSUPP

		"too large value", // ISO_ELARGE

		"not enough memory", // ISO_EMEM
		"no primary volume descriptor", // ISO_ENOPRIM
		"empty root directory in primary volume descriptor", // ISO_EPRIMEMPTY
		"can't convert name to UTF-8", // ISO_ENAME

		"", // ISO_ENOTREADY
		"", // ISO_EDIRORDER
	};

	ffuint e = o->err - 1;
	if (e > FF_COUNT(errs))
		return "";
	return errs[e];
}

#define _FFISO_ERR(o, n) \
	(o)->err = (n),  FFISOREAD_ERROR

#define _FFISO_GATHER(o, st) \
	(o)->state = ISOR_GATHER,  (o)->nextstate = st

static inline void ffisoread_open(ffisoread *o)
{
	fflist_init(&o->files);
}

static inline void ffisoread_close(ffisoread *o)
{
	ffchain_item *it;
	FFLIST_FOR(&o->files, it) {
		ffisoread_fileinfo_t *f = FF_STRUCTPTR(ffisoread_fileinfo_t, sib, it);
		it = it->next;
		ffstr_free(&f->name);
		ffmem_free(f);
	}
	ffvec_free(&o->fn);
	ffvec_free(&o->fullfn);
	ffvec_free(&o->buf);
}

static void _ffisoread_log(ffisoread *o, const char *fmt, ...)
{
	if (o->log == NULL)
		return;

	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	o->log(o->udata, 0, s);
	ffstr_free(&s);
}

enum {
	ISOR_SEEK_PRIM, ISOR_PRIM, ISOR_VOLDESC,
	ISOR_ENT_SEEK, ISOR_ENT,
	ISOR_FDATA_SEEK, ISOR_FDATA, ISOR_FDONE,
	ISOR_GATHER,
};

static ffisoread_fileinfo_t* ffisoread_nextfile(ffisoread *o);

/**
Return 0 if no more entries */
static int _ffisoread_read_next_dir(ffisoread *o)
{
	ffisoread_fileinfo_t *f;
	for (;;) {
		if (NULL == (f = ffisoread_nextfile(o)))
			return 0;
		if (f->attr & ISO_FILE_DIR) {
			o->curdir = f;
			o->inoff = f->off;
			o->fsize = f->size;
			o->state = ISOR_ENT_SEEK;
			return 1;
		}
	}
}

/**
Return enum FFISOREAD_R */
/* ISO-9660 read:
. Seek to 17th sector
. Read primary volume descriptor
. Read other volume descriptors (FFISOREAD_HDR)
. Seek to root dir entry
. Read entries from root dir and its subdirectories (FFISOREAD_FILEMETA, FFISOREAD_LISTEND)

After user has requested file data:
. Seek to the needed sector
. Read file data (FFISOREAD_DATA, FFISOREAD_FILEDONE)
*/
static inline int ffisoread_process(ffisoread *o, ffstr *input, ffstr *output)
{
	int r;

	for (;;) {
		switch (o->state) {

		case ISOR_GATHER:
			r = ffstr_gather((ffstr*)&o->buf, &o->buf.cap, input->ptr, input->len, ISO_SECTOR, &o->d);
			if (r < 0)
				return _FFISO_ERR(o, ISO_EMEM);
			ffstr_shift(input, r);
			FF_ASSERT((o->inoff % ISO_SECTOR) == 0);
			if (o->d.len == 0)
				return FFISOREAD_MORE;
			o->inoff += ISO_SECTOR;
			o->buf.len = 0;
			o->state = o->nextstate;
			continue;

		case ISOR_SEEK_PRIM:
			_FFISO_GATHER(o, ISOR_PRIM);
			o->inoff = 16 * ISO_SECTOR;
			return FFISOREAD_SEEK;

		case ISOR_PRIM: {
			if ((ffbyte)o->d.ptr[0] != ISO_T_PRIM)
				return _FFISO_ERR(o, ISO_ENOPRIM);
			if (0 != (r = iso_voldesc_prim_read(o->d.ptr, o)))
				return _FFISO_ERR(o, -r);

			const struct iso_voldesc_prim *prim = (struct iso_voldesc_prim*)(o->d.ptr + sizeof(struct iso_voldesc));
			ffisoread_fileinfo_t f;
			r = iso_ent_read(prim->root_dir, sizeof(prim->root_dir), &f, o->inoff - ISO_SECTOR, o);
			if (r < 0)
				return _FFISO_ERR(o, -r);
			else if (r == 0)
				return _FFISO_ERR(o, ISO_EPRIMEMPTY);

			o->root_start = f.off;
			o->fsize = f.size;

			_FFISO_GATHER(o, ISOR_VOLDESC);
			break;
		}

		case ISOR_VOLDESC: {
			ffuint t = (ffbyte)o->d.ptr[0];
			_ffisoread_log(o, "Vol Desc: %xu", t);

			if (t == ISO_T_JOLIET && !(o->options & FFISOREAD_NO_JOLIET)
				&& 0 == iso_voldesc_prim_read(o->d.ptr, o)) {
				const struct iso_voldesc_prim *jlt = (struct iso_voldesc_prim*)(o->d.ptr + sizeof(struct iso_voldesc));
				ffisoread_fileinfo_t f;
				r = iso_ent_read(jlt->root_dir, sizeof(jlt->root_dir), &f, o->inoff - ISO_SECTOR, o);
				if (r > 0) {
					if (NULL == ffvec_alloc(&o->fn, 255 * 4, 1))
						return _FFISO_ERR(o, ISO_EMEM);
					o->root_start = f.off;
					o->fsize = f.size;
					o->joliet = 1;
				}

			} else if (t == ISO_T_TERM) {
				o->inoff = o->root_start;
				o->state = ISOR_ENT_SEEK;
				return FFISOREAD_HDR;
			}

			_FFISO_GATHER(o, ISOR_VOLDESC);
			break;
		}


		case ISOR_ENT_SEEK:
			_FFISO_GATHER(o, ISOR_ENT);
			return FFISOREAD_SEEK;

		case ISOR_ENT:
			if (o->d.len == 0) {
				if (o->fsize == 0) {
					r = _ffisoread_read_next_dir(o);
					if (r == 0) {
						o->curdir = NULL;
						o->fcursor = NULL;
						return FFISOREAD_LISTEND;
					}
					continue;
				}
				_FFISO_GATHER(o, ISOR_ENT);
				continue;
			}

			r = iso_ent_read(o->d.ptr, o->d.len, &o->curfile, o->inoff - ISO_SECTOR, o);
			if (r < 0)
				return _FFISO_ERR(o, -r);
			else if (r == 0) {
				o->fsize = ffmax((ffint64)(o->fsize - o->d.len), 0);
				o->d.len = 0;
				continue;
			}
			ffstr_shift(&o->d, r);
			o->fsize -= r;

			if (o->joliet) {
				int n = ffutf8_from_utf16((char*)o->fn.ptr, o->fn.cap, o->curfile.name.ptr, o->curfile.name.len, FFUNICODE_UTF16BE);
				if (n < 0)
					return _FFISO_ERR(o, -ISO_ENAME);
				ffstr_set(&o->curfile.name, o->fn.ptr, n);
			} else {
				o->curfile.name = iso_ent_name(&o->curfile.name);
			}

			if (!(o->options & FFISOREAD_NO_RR)) {
				void *ent = o->d.ptr - r;
				ffstr rr;
				ffstr_set(&rr, ent, r);
				ffstr_shift(&rr, iso_ent_len(ent));
				iso_rr_read(rr.ptr, rr.len, &o->curfile, o);
			}

			if (o->curfile.name.len == 0)
				continue;

			// "name" -> "curdir/name"
			if (o->curdir != NULL) {
				o->fullfn.len = 0;
				ffvec_addfmt(&o->fullfn, "%S/%S", &o->curdir->name, &o->curfile.name);
				ffstr_set2(&o->curfile.name, &o->fullfn);
			}

			return FFISOREAD_FILEMETA;


		case ISOR_FDATA_SEEK:
			_FFISO_GATHER(o, ISOR_FDATA);
			return FFISOREAD_SEEK;

		case ISOR_FDATA: {
			ffuint n = ffmin(o->d.len, o->fsize);
			if (o->fsize <= o->d.len)
				o->state = ISOR_FDONE;
			else
				_FFISO_GATHER(o, ISOR_FDATA);
			ffstr_set(output, o->d.ptr, n);
			o->fsize -= n;
			return FFISOREAD_DATA;
		}

		case ISOR_FDONE:
			return FFISOREAD_FILEDONE;
		}
	}
}

#undef _FFISO_ERR
#undef _FFISO_GATHER

/** Get next file header stored in contents table.
Return NULL if no more files */
static inline ffisoread_fileinfo_t* ffisoread_nextfile(ffisoread *o)
{
	if (o->fcursor == NULL)
		o->fcursor = fflist_first(&o->files);
	else
		o->fcursor = o->fcursor->next;
	if (o->fcursor == fflist_sentl(&o->files))
		return NULL;
	ffisoread_fileinfo_t *f = FF_STRUCTPTR(ffisoread_fileinfo_t, sib, o->fcursor);
	return f;
}

/** Save file header in contents table */
static inline int ffisoread_storefile(ffisoread *o)
{
	ffisoread_fileinfo_t *f;
	if (NULL == (f = ffmem_new(ffisoread_fileinfo_t)))
		return -1;
	*f = o->curfile;
	ffstr_null(&f->name);
	if (NULL == ffstr_dupstr(&f->name, &o->curfile.name)) {
		ffmem_free(f);
		return -1;
	}
	fflist_add(&o->files, &f->sib);
	return 0;
}

/** Start reading a file */
static inline void ffisoread_readfile(ffisoread *o, ffisoread_fileinfo_t *f)
{
	if (f->attr & ISO_FILE_DIR) {
		o->state = ISOR_FDONE;
		return;
	}
	o->inoff = f->off;
	o->fsize = f->size;
	o->state = ISOR_FDATA_SEEK;
}

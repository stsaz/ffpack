/** ffpack: .iso writer (ISO 9660 + Joliet, Rock Ridge)
* no UNIX file attributes, uid, gid

2017,2021, Simon Zolin
*/

/*
ffisowrite_create
ffisowrite_close
ffisowrite_fileadd
ffisowrite_filenext
ffisowrite_process
ffisowrite_offset
ffisowrite_finish
ffisowrite_error
*/

#pragma once

#include <ffpack/iso-fmt.h>
#include <ffbase/vector.h>
#include <ffbase/map.h>

typedef struct iso_file ffisowrite_fileinfo_t;

struct _ffiso_pathtab {
	ffuint size, off_le, off_be;
};

typedef struct ffisowrite {
	ffuint state;
	int err;
	ffuint64 off;
	ffvec buf;
	struct _ffiso_pathtab pathtab, pathtab_jlt;
	ffvec dirs; // struct _ffiso_dir[]
	ffvec dirs_jlt; // struct _ffiso_dir[]
	ffuint idir;
	int ifile;
	ffmap dirnames; // "_ffiso_dir/name" -> ffsize idir -> struct _ffiso_dir*
	ffuint nsectors;
	ffuint64 curfile_size;
	const char *name; // Volume name
	ffuint options; // enum FFISOWRITE_OPT
	ffuint filedone :1;
} ffisowrite;

enum FFISOWRITE_OPT {
	FFISOWRITE_NO_JOLIET = 1, // don't parse Joliet extensions
	FFISOWRITE_NO_RR = 2, // don't parse RR extensions
};

#define ffisowrite_offset(w)  ((w)->off)

struct _ffiso_dir {
	ffisowrite_fileinfo_t info;
	ffuint name_off; // offset to file name part in info.name
	ffuint parent_dir;
	ffuint ifile; // index in parent's files array representing this directory
	ffvec files; // iso_file[]
};

static inline const char* ffisowrite_error(ffisowrite *w)
{
	static const char* const errs[] = {
		"", // ISO_ELOGBLK
		"", // ISO_EPRIMID
		"", // ISO_EPRIMVER
		"", // ISO_EUNSUPP

		"too large value", // ISO_ELARGE

		"not enough memory", // ISO_EMEM
		"no primary volume descriptor", // ISO_ENOPRIM
		"empty root directory in primary volume descriptor", // ISO_EPRIMEMPTY
		"", // ISO_ENAME

		"not ready", // ISO_ENOTREADY
		"invalid order of directories", // ISO_EDIRORDER
	};

	ffuint e = w->err - 1;
	if (e > FF_COUNT(errs))
		return "";
	return errs[e];
}

static void _ffisowrite_log(ffisowrite *w, const char *fmt, ...)
{
	(void)w; (void)fmt;
#ifdef FFPACK_DEBUG
	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	printf("%.*s\n", (int)s.len, s.ptr);
	ffstr_free(&s);
#endif
}

static struct _ffiso_dir* _ffisowrite_dir_new(ffisowrite *w, ffstr *name);

static int _ffisowrite_mapdir_keyeq(void *opaque, const void *key, ffsize keylen, void *val)
{
	ffisowrite *w = (ffisowrite*)opaque;
	const struct _ffiso_dir *d = ffslice_itemT(&w->dirs, (ffsize)val - 1, struct _ffiso_dir);
	return ffstr_eq(&d->info.name, key, keylen);
}

/**
flags: enum FFISO_OPT
Return 0 on success */
static inline int ffisowrite_create(ffisowrite *w, const char *name, ffuint flags)
{
	if (NULL == ffvec_alloc(&w->buf, 16 * ISO_SECTOR, 1))
		return -1;
	w->ifile = -1;
	w->options = flags;
	ffmap_init(&w->dirnames, _ffisowrite_mapdir_keyeq);
	w->name = (name != NULL) ? name : "CDROM";

	ffstr s = {};
	if (NULL == _ffisowrite_dir_new(w, &s))
		return ISO_EMEM;
	return 0;
}

static inline void ffisowrite_close(ffisowrite *w)
{
	struct _ffiso_dir *d;
	ffisowrite_fileinfo_t *f;
	FFSLICE_WALK(&w->dirs, d) {
		FFSLICE_WALK(&d->files, f) {
			ffstr_free(&f->name);
		}
		ffvec_free(&d->files);
		ffstr_free(&d->info.name);
	}
	ffvec_free(&w->dirs);

	FFSLICE_WALK(&w->dirs_jlt, d) {
		ffvec_free(&d->files);
	}
	ffvec_free(&w->dirs_jlt);

	ffmap_free(&w->dirnames);
	ffvec_free(&w->buf);
}

/** Set offset and size for each directory */
static int _ffiso_dirs_count(ffvec *dirs, ffuint64 *off, ffuint flags)
{
	int r;
	ffuint sectsize = 0;
	ffuint64 size;
	struct _ffiso_dir *d, *parent;
	ffisowrite_fileinfo_t *f;

	FFSLICE_WALK(dirs, d) {

		size = iso_ent_len2(1) * 2;

		FFSLICE_WALK(&d->files, f) {
			ffuint flags2 = ((void*)d != dirs->ptr) ? flags : flags | ENT_WRITE_RR_SP;
			r = iso_ent_write(NULL, 0, f, flags2);
			if (r < 0)
				return -r;
			sectsize += r;
			if (sectsize > ISO_SECTOR) {
				size += ISO_SECTOR - (sectsize - r);
				sectsize = r;
			}
			size += r;
		}

		if (size > (ffuint)-1)
			return ISO_ELARGE;

		d->info.size = ffint_align_ceil2(size, ISO_SECTOR);
		d->info.off = *off;
		if ((void*)d != (void*)dirs->ptr) {
			// set info in the parent's file array
			parent = ffslice_itemT(dirs, d->parent_dir, struct _ffiso_dir);
			f = ffslice_itemT(&parent->files, d->ifile, ffisowrite_fileinfo_t);
			f->size = d->info.size;
			f->off = *off;
		}
		*off += d->info.size;
	}

	return 0;
}

/** Copy meta data of directories and files.  Names are not copied */
static int _ffiso_dirs_copy(ffvec *dst, ffvec *src)
{
	if (NULL == ffvec_allocT(dst, src->len, struct _ffiso_dir))
		return -1;

	struct _ffiso_dir *d, *jd = (struct _ffiso_dir*)dst->ptr;
	FFSLICE_WALK(src, d) {
		*jd = *d;
		ffvec_null(&jd->files);
		if (NULL == ffvec_allocT(&jd->files, d->files.len, ffisowrite_fileinfo_t))
			return -1;
		ffvec_add2T(&jd->files, &d->files, ffisowrite_fileinfo_t);
		jd++;
		dst->len++;
	}

	return 0;
}

/** Set offset for each file */
static void _ffiso_files_setoffsets(ffvec *dirs, ffuint64 off)
{
	(void)off;
	struct _ffiso_dir *d;
	ffisowrite_fileinfo_t *f;
	FFSLICE_WALK(dirs, d) {
		FFSLICE_WALK(&d->files, f) {
			if (!(f->attr & ISO_FILE_DIR)) {
				f->off = off;
				off += ffint_align_ceil2(f->size, ISO_SECTOR);
			}
		}
	}
}

/** Set offset and size for each directory.
Set offset for each file */
static int _ffisowrite_dirs_countall(ffisowrite *w, ffuint start_off)
{
	int r;
	ffuint64 off = start_off;
	ffuint flags = !(w->options & FFISOWRITE_NO_RR) ? ENT_WRITE_RR : 0;

	if (0 != (r = _ffiso_dirs_count(&w->dirs, &off, flags)))
		return r;

	if (!(w->options & FFISOWRITE_NO_JOLIET)) {
		if (0 != _ffiso_dirs_copy(&w->dirs_jlt, &w->dirs))
			return ISO_EMEM;

		if (0 != (r = _ffiso_dirs_count(&w->dirs_jlt, &off, ENT_WRITE_JLT)))
			return r;
	}

	_ffiso_files_setoffsets(&w->dirs, off);

	if (!(w->options & FFISOWRITE_NO_JOLIET))
		_ffiso_files_setoffsets(&w->dirs_jlt, off);

	return 0;
}

static int _ffisowrite_pathtab_count(ffisowrite *w, ffuint flags)
{
	int r;
	ffuint size = 0;
	ffstr name;

	struct _ffiso_dir *d;
	FFSLICE_WALK(&w->dirs, d) {
		name = d->info.name;
		ffstr_shift(&name, d->name_off);
		r = iso_pathentry_write(NULL, 0, &name, 0, 0, flags);
		if (r < 0)
			return -r;
		size += r;
	}

	return size;
}

static int _ffisowrite_pathtab_countall(ffisowrite *w)
{
	ffuint size, size_jlt = 0;
	size = _ffisowrite_pathtab_count(w, 0);
	size = ffint_align_ceil2(size, ISO_SECTOR);

	if (!(w->options & FFISOWRITE_NO_JOLIET)) {
		size_jlt = _ffisowrite_pathtab_count(w, ISO_PATHENT_WRITE_JLT);
		size_jlt = ffint_align_ceil2(size_jlt, ISO_SECTOR);
	}

	return 2 * size + 2 * size_jlt;
}

/** Write path table */
static int _ffisowrite_pathtab_write(ffisowrite *w, ffuint flags)
{
	int r;
	ffuint size = 0, size_al;
	const ffvec *dirs = (flags & ISO_PATHENT_WRITE_JLT) ? &w->dirs_jlt : &w->dirs;
	const struct _ffiso_dir *d;
	ffstr name, empty = FFSTR_INITN("\x00", 1);

	FFSLICE_WALK(dirs, d) {
		name = d->info.name;
		ffstr_shift(&name, d->name_off);
		const ffstr *nm = (name.len != 0) ? &name : &empty;
		r = iso_pathentry_write(NULL, 0, nm, 0, 0, flags);
		if (r < 0)
			return -r;
		size += r;
	}

	size_al = ffint_align_ceil2(size, ISO_SECTOR);
	w->buf.len = 0;
	if (NULL == ffvec_realloc(&w->buf, size_al, 1))
		return ISO_EMEM;
	ffmem_zero(w->buf.ptr, size_al);

	FFSLICE_WALK(dirs, d) {
		name = d->info.name;
		ffstr_shift(&name, d->name_off);
		const ffstr *nm = (name.len != 0) ? &name : &empty;
		r = iso_pathentry_write(ffslice_end(&w->buf, 1), ffvec_unused(&w->buf)
			, nm, d->info.off / ISO_SECTOR, d->parent_dir + 1, flags);
		if (r < 0)
			return -r;
		_ffisowrite_log(w, "name:%S  body-off:%xu  parent:%u"
			, nm, d->info.off, d->parent_dir + 1);
		w->buf.len += r;
	}

	ffuint off = w->off / ISO_SECTOR;
	struct _ffiso_pathtab *pt = (flags & ISO_PATHENT_WRITE_JLT) ? &w->pathtab_jlt : &w->pathtab;
	pt->size = size;
	if (flags & ISO_PATHENT_WRITE_BE)
		pt->off_be = off;
	else
		pt->off_le = off;

	w->buf.len = size_al;
	return 0;
}

static void _ffisowrite_ent_write_log(ffisowrite *w, const ffisowrite_fileinfo_t *f)
{
	struct iso_fileentry *ent = (struct iso_fileentry*)ffslice_end(&w->buf, 1);
	_ffisowrite_log(w, "Dir Ent: off:%xU  body-off:%xu  body-len:%xu  name:%S  length:%u"
		, w->off, f->off, f->size, &f->name, ent->len);
}

/** Write directory contents */
static int _ffisowrite_dir_write(ffisowrite *w, ffuint joliet)
{
	int r;
	ffvec *dirs = (joliet) ? &w->dirs_jlt : &w->dirs;
	const struct _ffiso_dir *d = ffslice_itemT(dirs, w->idir++, struct _ffiso_dir);
	ffuint flags = !(w->options & FFISOWRITE_NO_RR) ? ENT_WRITE_RR : 0;
	if (joliet)
		flags = ENT_WRITE_JLT;
	w->buf.len = 0;
	if (NULL == ffvec_realloc(&w->buf, d->info.size, 1))
		return ISO_EMEM;
	ffmem_zero(w->buf.ptr, d->info.size);

	struct iso_file f = {};

	f.off = d->info.off;
	f.size = d->info.size;
	f.attr = ISO_FILE_DIR;
	ffstr_set(&f.name, "\x00", 1);
	ffuint flags2 = (w->idir != 1) ? flags : flags | ENT_WRITE_RR_SP;
	r = iso_ent_write(ffslice_end(&w->buf, 1), ffvec_unused(&w->buf), &f, flags2);
	_ffisowrite_ent_write_log(w, &f);
	w->buf.len += r;

	ffstr_set(&f.name, "\x01", 1);
	const struct _ffiso_dir *parent = ffslice_itemT(dirs, d->parent_dir, struct _ffiso_dir);
	f.off = parent->info.off;
	f.size = parent->info.size;
	f.attr = ISO_FILE_DIR;
	r = iso_ent_write(ffslice_end(&w->buf, 1), ffvec_unused(&w->buf), &f, flags);
	_ffisowrite_ent_write_log(w, &f);
	w->buf.len += r;

	ffuint sectsize = w->buf.len;
	ffisowrite_fileinfo_t *pf;
	FFSLICE_WALK(&d->files, pf) {
		r = iso_ent_write(NULL, 0, pf, flags);
		sectsize += r;
		if (sectsize > ISO_SECTOR) {
			w->buf.len += ISO_SECTOR - (sectsize - r);
			sectsize = r;
		}

		r = iso_ent_write(ffslice_end(&w->buf, 1), ffvec_unused(&w->buf), pf, flags);
		_ffisowrite_ent_write_log(w, pf);
		w->buf.len += r;
	}

	w->buf.len = d->info.size;
	return 0;
}

static void _ffisowrite_voldesc_prim_write_log(ffisowrite *w, const struct iso_voldesc_prim_host *info)
{
	_ffisowrite_log(w, "Prim Vol Desc:  vol-size:%u  off:%xu  size:%xu  path-table:%xu %xu,%xu"
		, info->vol_size, info->root_dir_off, info->root_dir_size
		, info->path_tbl_size, info->path_tbl_off, info->path_tbl_off_be);
}

enum FFISOWRITE_R {
	FFISOWRITE_MORE,
	FFISOWRITE_ERROR,
	FFISOWRITE_SEEK,
	FFISOWRITE_DATA, // call ffiso_output() to get file data
	FFISOWRITE_DONE, // output .iso is finished
};

#define _ERR(o, e) \
	(o)->err = (e),  FFISOWRITE_ERROR

enum {
	ISOW_DIR_WAIT, ISOW_EMPTY, ISOW_EMPTY_VD,
	ISOW_PATHTAB, ISOW_PATHTAB_BE, ISOW_PATHTAB_JLT, ISOW_PATHTAB_JLT_BE,
	ISOW_DIR, ISOW_DIR_JLT, ISOW_FILE_NEXT, ISOW_FILE, ISOW_FILE_DONE,
	ISOW_VOLDESC_SEEK, ISOW_VOLDESC_PRIM, ISOW_VOLDESC_JLT, ISOW_VOLDESC_TERM, ISOW_DONE, ISOW_ERR,
};

/**
Note: no RR PX, no RR CL.
Return enum FFISOWRITE_R */
/* ISO-9660 write:
. Get the complete and sorted file list from user
 . ffisowrite_fileadd()
. Write empty 16 sectors (FFISOWRITE_DATA)
. Write 3 empty sectors for volume descriptors
. Calculate the length of path tables
. For each directory calculate offset and size of its contents
  Directory size is always a multiple of sector size.
  If size is larger than sector size, data is split into multiple blocks:
    #1: (file-entry)... (zero padding)
    #2: (file-entry)... (zero padding)
. Write path tables
. Write all directories contents
. Write files data
 . ffisowrite_filenext()
 . Write file data
. ffisowrite_finish()
. Seek to the beginning
. Write "primary" volume descriptor
. Write "joliet" volume descriptor
. Write "terminate" volume descriptor (FFISOWRITE_DATA, FFISOWRITE_DONE)

Example of placement of directory contents and file data:

primary-VD -> /
joliet-primary-VD -> JOLIET-/
term-VD

path-tables:
  "" -> /
  "d" -> /d
    parent=""

(JOLIET PATH TABLES)

/:
  "." -> /
    RR:SP RR:RR
  ".." -> /
    RR:RR
  "a" -> /a
    RR:RR RR:NM
  "d" -> /d
  "z" -> /z
  [zero padding up to sector size]

/d:
  "." -> /d
  ".." -> /
  "a" -> /d/a

(JOLIET DIR CONTENTS)

/a: <data>
/z: <data>
/d/a: <data>
*/
static inline int ffisowrite_process(ffisowrite *w, ffstr *input, ffstr *output)
{
	int r;

	for (;;) {
		switch (w->state) {

		case ISOW_DIR_WAIT:
			if (w->dirs.len != 0) {
				w->state = ISOW_EMPTY;
				continue;
			}
			return FFISOWRITE_MORE;

		case ISOW_EMPTY:
			ffmem_zero(w->buf.ptr, 16 * ISO_SECTOR);
			ffstr_set(output, w->buf.ptr, 16 * ISO_SECTOR);
			w->off += output->len;
			w->state = ISOW_EMPTY_VD;
			return FFISOWRITE_DATA;

		case ISOW_EMPTY_VD:
			ffstr_set(output, w->buf.ptr, 3 * ISO_SECTOR);
			w->off += output->len;
			r = _ffisowrite_pathtab_countall(w);
			if (r < 0)
				return r;
			if (0 != (r = _ffisowrite_dirs_countall(w, w->off + r)))
				return r;
			w->idir = 0;
			w->state = ISOW_PATHTAB;
			return FFISOWRITE_DATA;

		case ISOW_PATHTAB_JLT:
			if (w->options & FFISOWRITE_NO_JOLIET) {
				w->state = ISOW_DIR;
				continue;
			}
			//fallthrough
		case ISOW_PATHTAB:
		case ISOW_PATHTAB_BE:
		case ISOW_PATHTAB_JLT_BE: {
			ffuint f = (w->state == ISOW_PATHTAB_BE || w->state == ISOW_PATHTAB_JLT_BE)
				? ISO_PATHENT_WRITE_BE : 0;
			f |= (w->state == ISOW_PATHTAB_JLT || w->state == ISOW_PATHTAB_JLT_BE)
				? ISO_PATHENT_WRITE_JLT : 0;
			if (0 != (r = _ffisowrite_pathtab_write(w, f)))
				return _ERR(w, r);
			ffstr_set(output, w->buf.ptr, w->buf.len);
			w->off += output->len;
			w->state++;
			return FFISOWRITE_DATA;
		}

		case ISOW_DIR:
		case ISOW_DIR_JLT:
			if (w->idir == w->dirs.len) {
				w->idir = 0;
				if (w->state == ISOW_DIR && !(w->options & FFISOWRITE_NO_JOLIET)) {
					w->state = ISOW_DIR_JLT;
					continue;
				}
				w->state = ISOW_FILE_NEXT;
				return FFISOWRITE_MORE;
			}
			if (0 != (r = _ffisowrite_dir_write(w, (w->state == ISOW_DIR_JLT))))
				return _ERR(w, r);
			ffstr_set(output, w->buf.ptr, w->buf.len);
			w->off += output->len;
			return FFISOWRITE_DATA;

		case ISOW_FILE: {
			*output = *input;
			w->off += output->len;
			w->curfile_size += output->len;
			const struct _ffiso_dir *d = ffslice_itemT(&w->dirs, w->idir, struct _ffiso_dir);
			const ffisowrite_fileinfo_t *f = ffslice_itemT(&d->files, w->ifile, ffisowrite_fileinfo_t);
			if (w->curfile_size > f->size)
				return _ERR(w, ISO_ELARGE);
			if (w->curfile_size == f->size)
				w->state = ISOW_FILE_DONE;
			else if (input->len == 0)
				return FFISOWRITE_MORE;
			input->len = 0;
			return FFISOWRITE_DATA;
		}

		case ISOW_FILE_DONE:
			w->state = ISOW_FILE_NEXT;
			if (0 == (w->curfile_size % ISO_SECTOR))
				continue;
			ffmem_zero(w->buf.ptr, ISO_SECTOR);
			ffstr_set(output, w->buf.ptr, ISO_SECTOR - (w->curfile_size % ISO_SECTOR));
			w->off += output->len;
			return FFISOWRITE_DATA;

		case ISOW_FILE_NEXT:
			return FFISOWRITE_MORE;


		case ISOW_VOLDESC_SEEK:
			w->nsectors = w->off / ISO_SECTOR;
			w->off = 16 * ISO_SECTOR;
			w->state = ISOW_VOLDESC_PRIM;
			return FFISOWRITE_SEEK;

		case ISOW_VOLDESC_PRIM: {
			const struct _ffiso_dir *d = (struct _ffiso_dir*)w->dirs.ptr;
			struct iso_voldesc_prim_host info = {
				.type = ISO_T_PRIM,
				.name = w->name,
				.root_dir_off = (ffuint)d->info.off,
				.root_dir_size = (ffuint)d->info.size,
				.vol_size = w->nsectors,
				.path_tbl_size = w->pathtab.size,
				.path_tbl_off = w->pathtab.off_le,
				.path_tbl_off_be = w->pathtab.off_be,
			};
			iso_voldesc_prim_write(w->buf.ptr, &info);
			_ffisowrite_voldesc_prim_write_log(w, &info);

			ffstr_set(output, w->buf.ptr, ISO_SECTOR);
			w->off += ISO_SECTOR;
			w->state = !(w->options & FFISOWRITE_NO_JOLIET) ? ISOW_VOLDESC_JLT : ISOW_VOLDESC_TERM;
			return FFISOWRITE_DATA;
		}

		case ISOW_VOLDESC_JLT: {
			const struct _ffiso_dir *jd = (struct _ffiso_dir*)w->dirs_jlt.ptr;
			ffmem_zero(w->buf.ptr, ISO_SECTOR);
			struct iso_voldesc_prim_host info = {
				.type = ISO_T_JOLIET,
				.name = w->name,
				.root_dir_off = (ffuint)jd->info.off,
				.root_dir_size = (ffuint)jd->info.size,
				.vol_size = w->nsectors,
				.path_tbl_size = w->pathtab_jlt.size,
				.path_tbl_off = w->pathtab_jlt.off_le,
				.path_tbl_off_be = w->pathtab_jlt.off_be,
			};
			iso_voldesc_prim_write(w->buf.ptr, &info);
			_ffisowrite_voldesc_prim_write_log(w, &info);

			ffstr_set(output, w->buf.ptr, ISO_SECTOR);
			w->off += ISO_SECTOR;
			w->state = ISOW_VOLDESC_TERM;
			return FFISOWRITE_DATA;
		}

		case ISOW_VOLDESC_TERM:
			ffmem_zero(w->buf.ptr, ISO_SECTOR);
			iso_voldesc_write(w->buf.ptr, ISO_T_TERM);

			ffstr_set(output, w->buf.ptr, ISO_SECTOR);
			w->off += ISO_SECTOR;
			w->state = ISOW_DONE;
			return FFISOWRITE_DATA;

		case ISOW_DONE:
			return FFISOWRITE_DONE;

		case ISOW_ERR:
			return _ERR(w, ISO_ENOTREADY);
		}
	}
	return 0;
}

#undef _ERR

static struct _ffiso_dir* _ffisowrite_dir_new(ffisowrite *w, ffstr *name)
{
	struct _ffiso_dir *d = ffvec_pushT(&w->dirs, struct _ffiso_dir);
	if (d == NULL)
		return NULL;
	ffmem_zero_obj(d);
	d->info.name = *name;

	ffsize idir = d - (struct _ffiso_dir*)w->dirs.ptr + 1;
	ffmap_add(&w->dirnames, d->info.name.ptr, d->info.name.len, (void*)idir);
	return d;
}

static struct _ffiso_dir* _ffisowrite_dir_find(ffisowrite *w, const ffstr *path)
{
	void *idir = ffmap_find(&w->dirnames, path->ptr, path->len, w);
	if (idir == NULL)
		return NULL;
	struct _ffiso_dir *d = ffslice_itemT(&w->dirs, (ffsize)idir - 1, struct _ffiso_dir);
	return d;
}

/** Add a new file
Files inside directories must be added after all files in parent directory are added
 ("a", "_ffiso_dir", "z", and only then "_ffiso_dir/file")
Return 0 on success */
static inline int ffisowrite_fileadd(ffisowrite *w, const struct iso_file *f)
{
	struct _ffiso_dir *parent, *d;
	struct iso_file *nf;
	ffstr path, name, fn = {};

	if (w->state != ISOW_DIR_WAIT) {
		w->err = ISO_ENOTREADY;
		goto err;
	}

	if (NULL == ffstr_alloc(&fn, f->name.len)) {
		w->err = ISO_EMEM;
		goto err;
	}
	fn.len = _ffpack_path_normalize(fn.ptr, f->name.len, f->name.ptr, f->name.len, _FFPACK_PATH_FORCE_SLASH | _FFPACK_PATH_SIMPLE);
	if (fn.len != 0 && *ffstr_last(&fn) == '/')
		fn.len--;

	_ffpack_path_splitpath_unix(fn.ptr, fn.len, &path, &name);
	parent = _ffisowrite_dir_find(w, &path);
	if (parent == NULL) {
		w->err = ISO_EDIRORDER; // trying to add "_ffiso_dir/file" with no "_ffiso_dir" added previously
		goto err;
	}

	if (f->attr & ISO_FILE_DIR) {
		ffuint i = parent - (struct _ffiso_dir*)w->dirs.ptr;
		if (NULL == (d = _ffisowrite_dir_new(w, &fn))) {
			w->err = ISO_EMEM;
			goto err;
		}
		d->name_off = (path.len != 0) ? path.len+1 : 0;
		d->parent_dir = i;
		parent = ffslice_itemT(&w->dirs, d->parent_dir, struct _ffiso_dir);
		d->ifile = parent->files.len;
		ffstr_null(&fn);
	}

	nf = ffvec_pushT(&parent->files, ffisowrite_fileinfo_t);
	if (nf == NULL) {
		w->err = ISO_EMEM;
		goto err;
	}
	ffmem_zero_obj(nf);
	if (NULL == ffstr_dupstr(&nf->name, &name)) {
		parent->files.len--;
		w->err = ISO_EMEM;
		goto err;
	}
	ffstr_free(&fn);
	nf->attr = f->attr;
	nf->mtime = f->mtime;
	nf->size = f->size;
	return 0;

err:
	ffstr_free(&fn);
	return -1;
}

static struct iso_file* _ffisowrite_file_next(ffisowrite *w)
{
	struct _ffiso_dir *d = (struct _ffiso_dir*)w->dirs.ptr;
	w->ifile++;

	for (ffuint i = w->idir;  i != w->dirs.len;  i++) {

		ffisowrite_fileinfo_t *f = (struct iso_file*)d[i].files.ptr;

		for (ffuint k = w->ifile;  k != d[i].files.len;  k++) {

			if (!(f[k].attr & ISO_FILE_DIR)) {
				w->idir = i;
				w->ifile = k;
				return &f[k];
			}
		}
		w->ifile = 0;
	}
	return NULL;
}

/** Prepare to add a new file data */
static inline void ffisowrite_filenext(ffisowrite *w)
{
	if (w->state != ISOW_FILE_NEXT) {
		w->err = ISO_ENOTREADY;
		w->state = ISOW_ERR;
		return;
	}

	const ffisowrite_fileinfo_t *f = _ffisowrite_file_next(w);
	if (f == NULL) {
		w->err = ISO_ENOTREADY;
		w->state = ISOW_ERR;
		return;
	}

	w->state = ISOW_FILE;
	w->curfile_size = 0;
}

/** All input data is processed */
static inline void ffisowrite_finish(ffisowrite *w)
{
	if (w->state != ISOW_FILE_NEXT) {
		w->err = ISO_ENOTREADY;
		w->state = ISOW_ERR;
		return;
	}
	w->state = ISOW_VOLDESC_SEEK;
}

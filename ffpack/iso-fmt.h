/** ffpack: .iso format
2018,2021, Simon Zolin
*/

/*
iso_voldesc_write
iso_voldesc_prim_read iso_voldesc_prim_write
iso_ent_len iso_ent_len2
iso_ent_read iso_ent_write
iso_ent_name
iso_rr_read
iso_pathentry_write
*/

/* .iso format:
16 * sector
prim_vol_desc vol_desc... [Joliet_vol_desc] term_vol_desc
path_table_LE | path_table_BE
[path_table_Jlt_LE | path_table_Jlt_BE]
((dir_ent [RR_ext...]) | [dir_ent_Jlt] | file_data)...
*/

#pragma once

#include <ffpack/path.h>
#include <ffbase/stringz.h>
#include <ffbase/unicode.h>
#include <ffbase/time.h>
#include <ffbase/chain.h>

#define ISO_FILE_DIR  0040000
#define ISO_SECTOR  2048

enum ISO_E {
	ISO_EOK,

	ISO_ELOGBLK,
	ISO_EPRIMID,
	ISO_EPRIMVER,
	ISO_EUNSUPP,

	ISO_ELARGE,

	ISO_EMEM,
	ISO_ENOPRIM,
	ISO_EPRIMEMPTY,
	ISO_ENAME,

	ISO_ENOTREADY,
	ISO_EDIRORDER,
};

struct ffisoread;
static void _ffisoread_log(struct ffisoread *o, const char *fmt, ...);

enum ISO_TYPE {
	ISO_T_PRIM = 1,
	ISO_T_JOLIET = 2,
	ISO_T_TERM = 0xff,
};

struct iso_voldesc {
	ffbyte type; // enum ISO_TYPE
	char id[5]; // "CD001"
	ffbyte ver; // =1
	ffbyte data[0];
};

struct iso_date {
	ffbyte year; // +1900
	ffbyte month;
	ffbyte day;
	ffbyte hour;
	ffbyte min;
	ffbyte sec;
	ffbyte gmt15;
};

enum ISO_F {
	ISO_FDIR = 2,
	ISO_FLARGE = 0x80,
};

struct iso_fileentry {
	ffbyte len;
	ffbyte ext_attr_len;
	ffbyte body_off[8]; // LE,BE
	ffbyte body_len[8]; // LE,BE
	struct iso_date date;
	ffbyte flags; // enum ISO_F
	ffbyte unused[2];
	ffbyte vol_seqnum[4];
	ffbyte namelen;
	/* files: "NAME.EXT;NUM"
	dirs: 0x00 (self) | 0x01 (parent) | "NAME" */
	ffbyte name[0];
	// ffbyte pad; // exists if namelen is even
};

/** Get length of file entry before RR extensions */
static inline ffuint iso_ent_len(const void *ent)
{
	const struct iso_fileentry *e = (struct iso_fileentry*)ent;
	return FF_OFF(struct iso_fileentry, name) + e->namelen + !(e->namelen % 2);
}
static inline ffuint iso_ent_len2(ffuint namelen)
{
	return FF_OFF(struct iso_fileentry, name) + namelen + !(namelen % 2);
}

#define ISO_SYSNAME  "LINUX"
#define ISO_UCS2L3_ESCSEQ  "%/E"

struct iso_voldesc_prim {
	ffbyte unused;
	ffbyte system[32];
	ffbyte name[32];
	ffbyte unused3[8];
	ffbyte vol_size[8]; // LE[4],BE[4]
	ffbyte esc_seq[32];
	ffbyte vol_set_size[4];
	ffbyte vol_set_seq[4];
	ffbyte log_blksize[4]; // LE[2],BE[2]
	ffbyte path_tbl_size[8]; // LE,BE
	ffbyte path_tbl1_off[4]; // LE
	ffbyte path_tbl2_off[4]; // LE
	ffbyte path_tbl1_off_be[4]; // BE
	ffbyte unused8[4];
	ffbyte root_dir[34]; // struct iso_fileentry
};

/** Parse primary volume descriptor */
static inline int iso_voldesc_prim_read(const void *p, void *log_param)
{
	const struct iso_voldesc *vd = (struct iso_voldesc*)p;
	if (ffmem_cmp(vd->id, "CD001", 5))
		return -ISO_EPRIMID;
	if (vd->ver != 1)
		return -ISO_EPRIMVER;

	const struct iso_voldesc_prim *prim = (struct iso_voldesc_prim*)vd->data;

	_ffisoread_log((struct ffisoread*)log_param, "Prim Vol Desc:  vol-size:%u  log-blk-size:%u  path-tbl-size:%u  name:%*s"
		, ffint_le_cpu32_ptr(prim->vol_size),  ffint_le_cpu16_ptr(prim->log_blksize), ffint_le_cpu32_ptr(prim->path_tbl_size)
		, (ffsize)sizeof(prim->name), prim->name);

	if (ffint_le_cpu16_ptr(prim->log_blksize) != ISO_SECTOR)
		return -ISO_ELOGBLK;

	return 0;
}

static inline void* iso_voldesc_write(void *buf, ffuint type)
{
	struct iso_voldesc *vd = (struct iso_voldesc*)buf;
	vd->type = type;
	ffmem_copy(vd->id, "CD001", 5);
	vd->ver = 1;
	return vd->data;
}

static void iso_write32(ffbyte *dst, ffuint val)
{
	*(ffuint*)dst = ffint_le_cpu32(val);
	*(ffuint*)&dst[4] = ffint_be_cpu32(val);
}
static void iso_write16(ffbyte *dst, ffuint val)
{
	*(ffushort*)dst = ffint_le_cpu16(val);
	*(ffushort*)&dst[2] = ffint_be_cpu16(val);
}

static void iso_writename(void *dst, ffsize cap, const char *s)
{
	ffuint n = _ffs_copyz((char*)dst, cap, s);
	ffmem_fill(&((char*)dst)[n], ' ', cap - n);
}

static void iso_writename16(void *dst, ffsize cap, const char *s)
{
	char *d = (char*)dst;
	ffuint i = ffutf8_to_utf16(d, cap, s, ffsz_len(s), FFUNICODE_UTF16BE);
	for (;  i != cap;  i += 2) {
		d[i] = '\0';
		d[i + 1] = ' ';
	}
}

struct iso_file {
	ffstr name;
	fftime mtime; // seconds since 1970
	ffuint attr;
	// ffuint attr_unix;
	// ffuint uid, gid;
	ffuint64 off;
	ffuint64 size;
	ffchain_item sib;
};

static int iso_ent_write(void *buf, ffsize cap, const struct iso_file *f, ffuint flags);

struct iso_voldesc_prim_host {
	ffuint type;
	const char *name;
	ffuint root_dir_off;
	ffuint root_dir_size;
	ffuint vol_size;
	ffuint path_tbl_size;
	ffuint path_tbl_off;
	ffuint path_tbl_off_be;
};

static inline void iso_voldesc_prim_write(void *buf, const struct iso_voldesc_prim_host *info)
{
	ffmem_zero(buf, ISO_SECTOR);
	struct iso_voldesc_prim *prim = (struct iso_voldesc_prim*)iso_voldesc_write(buf, info->type);
	if (info->type == ISO_T_JOLIET) {
		iso_writename16(prim->system, sizeof(prim->system), ISO_SYSNAME);
		iso_writename16(prim->name, sizeof(prim->name), info->name);
		ffmem_copy(prim->esc_seq, ISO_UCS2L3_ESCSEQ, 3);
	} else {
		iso_writename(prim->system, sizeof(prim->system), ISO_SYSNAME);
		iso_writename(prim->name, sizeof(prim->name), info->name);
	}
	iso_write32(prim->vol_size, info->vol_size);
	iso_write16(prim->vol_set_size, 1);
	iso_write16(prim->vol_set_seq, 1);
	iso_write16(prim->log_blksize, ISO_SECTOR);

	iso_write32(prim->path_tbl_size, info->path_tbl_size);
	*(ffuint*)prim->path_tbl1_off = ffint_le_cpu32(info->path_tbl_off);
	*(ffuint*)prim->path_tbl1_off_be = ffint_be_cpu32(info->path_tbl_off_be);

	struct iso_file f = {};
	ffstr_set(&f.name, "\x00", 1);
	f.off = info->root_dir_off;
	f.size = info->root_dir_size;
	f.attr = ISO_FILE_DIR;
	iso_ent_write(prim->root_dir, sizeof(prim->root_dir), &f, 0);
}


/** Get real filename */
static inline ffstr iso_ent_name(const ffstr *name)
{
	ffstr s, ver;
	ffstr_rsplitby(name, ';', &s, &ver);
	if (ffstr_eqcz(&ver, "1")) {
		if (s.len != 0 && *ffstr_last(&s) == '.')
			s.len--; // "NAME." -> "NAME"
		return s;
	}
	return *name;
}

static ffsize iso_copyname(char *dst, const char *end, const char *src, ffsize len)
{
	ffsize i;
	len = ffmin(len, end - dst);

	for (i = 0;  i != len;  i++) {
		int ch = src[i];

		if ((ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z'))
			dst[i] = ch & ~0x20;
		else if (ch >= '0' && ch <= '9')
			dst[i] = ch;
		else
			dst[i] = '_';
	}

	return i;
}

/*
Filename with extension: "NAME.EXT;1"
Filename without extension: "NAME."
Directory name: "NAME"
*/
static ffuint iso_ent_name_write(char *dst, const ffstr *filename, ffuint attr)
{
	ffstr name, ext = {};
	_ffpack_path_splitname(filename->ptr, filename->len, &name, &ext);
	name.len = ffmin(name.len, 8);
	ext.len = ffmin(ext.len, 3);
	ffuint fnlen = name.len + ext.len;
	if (!(attr & ISO_FILE_DIR) || ext.len != 0)
		fnlen += FFS_LEN(".");
	if (!(attr & ISO_FILE_DIR))
		fnlen += FFS_LEN(";1");

	if (dst == NULL)
		return fnlen;

	char *p = dst;
	const char *end = dst + fnlen;
	p += iso_copyname(p, end, name.ptr, name.len);
	if (!(attr & ISO_FILE_DIR) || ext.len != 0)
		*p++ = '.';
	p += iso_copyname(p, end, ext.ptr, ext.len);
	if (!(attr & ISO_FILE_DIR))
		_ffs_copy(p, end-p, ";1", 2);
	return fnlen;
}


struct iso_rr {
	char id[2];
	ffbyte len;
	ffbyte ver; //=1
	ffbyte data[0];
};

enum ISO_RR_FLAGS {
	ISO_RR_HAVE_PX = 1,
	ISO_RR_HAVE_NM = 8,
};

// "SP"
struct iso_rr_sp {
	ffbyte data[3];
};

// "RR"
struct iso_rr_rr {
	ffbyte flags; // enum ISO_RR_FLAGS
};

enum ISO_RR_NM_F {
	ISO_RR_NM_FCONTINUE = 1,
};

// [NM(flags:CONTINUE=1)...] NM(flags:CONTINUE=0)
struct iso_rr_nm {
	// "NM"
	ffbyte flags; // enum ISO_RR_NM_F
	ffbyte data[0];
};

struct iso_rr_px {
	// "PX"
	ffbyte mode[8]; // UNIX file mode
	ffbyte unused2[8];
	ffbyte uid[8];
	ffbyte gid[8];
};

struct iso_rr_cl {
	// "CL"
	ffbyte child_off[8];
};

/** Parse Rock-Ridge extension entries
Return # of bytes read
 <0 on error */
static inline int iso_rr_read(const void *p, ffsize len, struct iso_file *f, void *log_param)
{
	const char *d = (char*)p, *end = (char*)p + len;
	ffuint dlen;
	for (;;) {

		if (d == end || d[0] == 0x00)
			break;

		const struct iso_rr *rr = (struct iso_rr*)d;
		if ((ffsize)(end - d) < sizeof(struct iso_rr)
			|| (end - d) < rr->len
			|| rr->len <= sizeof(struct iso_rr))
			return -ISO_ELARGE;
		d += rr->len;
		dlen = rr->len - sizeof(struct iso_rr);

		_ffisoread_log((struct ffisoread*)log_param, "RR ext: %2s  size:%u", rr->id, rr->len);

		if (!ffmem_cmp(rr->id, "NM", 2)) {
			if (dlen < sizeof(struct iso_rr_nm))
				continue;
			const struct iso_rr_nm *nm = (struct iso_rr_nm*)(rr + 1);
			FF_ASSERT(nm->flags == 0);
			if (nm->flags != 0)
				return -ISO_EUNSUPP;
			ffstr_set(&f->name, nm->data, (ffbyte*)d - nm->data);

		} else if (!ffmem_cmp(rr->id, "PX", 2)) {
			if (dlen < sizeof(struct iso_rr_px))
				continue;
			const struct iso_rr_px *px = (struct iso_rr_px*)(rr + 1);
			f->attr = ffint_le_cpu32_ptr(px->mode);

		} else if (!ffmem_cmp(rr->id, "CL", 2)) {
			if (dlen < sizeof(struct iso_rr_cl))
				continue;
			const struct iso_rr_cl *cl = (struct iso_rr_cl*)(rr + 1);
			f->off = ffint_le_cpu32_ptr(cl->child_off) * ISO_SECTOR;
			_ffisoread_log((struct ffisoread*)log_param, "RR CL: off:%xU", f->off);

		} else if (!ffmem_cmp(rr->id, "RE", 2)) {
			if (dlen < 1)
				continue;
			f->name.len = 0;
		}
	}

	return d - (char*)p;
}

static int iso_rr_write(void *dst, const char *name, ffuint datalen)
{
	FF_ASSERT(datalen + sizeof(struct iso_rr) < 255);

	struct iso_rr *rr = (struct iso_rr*)dst;
	ffmem_copy(rr->id, name, 2);
	rr->len = datalen + sizeof(struct iso_rr);
	rr->ver = 1;
	return sizeof(struct iso_rr);
}


/** Parse date from file entry */
static void iso_date_read(const struct iso_date *d, ffdatetime *dt)
{
	dt->year = 1900 + d->year;
	dt->month = d->month;
	dt->day = d->day;
	dt->hour = d->hour;
	dt->minute = d->min;
	dt->second = d->sec;
	dt->nanosecond = 0;
	dt->weekday = 0;
	dt->yearday = 0;
}

/** Write file entry date */
static void iso_date_write(struct iso_date *d, const ffdatetime *dt)
{
	d->year = dt->year - 1900;
	d->month = dt->month;
	d->day = dt->day;
	d->hour = dt->hour;
	d->min = dt->minute;
	d->sec = dt->second;
}

/** Parse file entry
Return entry length
 0: no more entries
 <0 on error */
static inline int iso_ent_read(const void *p, ffsize len, struct iso_file *f, ffuint64 off, void *log_param)
{
	const struct iso_fileentry *ent = (struct iso_fileentry*)p;

	if (len != 0 && ((ffbyte*)p)[0] == 0)
		return 0;

	if (len < sizeof(struct iso_fileentry)
		|| len < ent->len
		|| ent->len < iso_ent_len(ent)
		|| ent->namelen == 0)
		return -ISO_ELARGE;

	_ffisoread_log((struct ffisoread*)log_param, "Dir Ent: off:%xU  body-off:%xu  body-len:%xu  flags:%xu  ext_attr_len:%u  length:%u  RR-len:%u  name:%*s"
		, off, ffint_le_cpu32_ptr(ent->body_off) * ISO_SECTOR, ffint_le_cpu32_ptr(ent->body_len)
		, ent->flags, ent->ext_attr_len
		, ent->len, ent->len - iso_ent_len(ent)
		, (ffsize)ent->namelen, ent->name);

	if (ent->ext_attr_len != 0)
		return -ISO_EUNSUPP;

	if (f == NULL)
		goto done;

	f->off = ffint_le_cpu32_ptr(ent->body_off) * ISO_SECTOR;
	f->size = ffint_le_cpu32_ptr(ent->body_len);
	ffstr_set(&f->name, ent->name, ent->namelen);
	f->attr = 0;

	if (ent->flags & ISO_FDIR) {
		if (ent->namelen == 1 && (ent->name[0] == 0x00 || ent->name[0] == 0x01))
			f->name.len = 0;
		f->attr = ISO_FILE_DIR;
	}

	ffdatetime dt;
	iso_date_read(&ent->date, &dt);
	fftime_join1(&f->mtime, &dt);
	f->mtime.sec -= FFTIME_1970_SECONDS;

done:
	return ent->len;
}

enum ENT_WRITE_F {
	ENT_WRITE_RR = 1,
	ENT_WRITE_JLT = 2,
	// ENT_WRITE_CUT = 4, // cut large filenames, don't return error
	ENT_WRITE_RR_SP = 8,
};

/** Write file entry
buf: must be filled with zeros
 if NULL: return output space required
flags: enum ENT_WRITE_F
Return bytes written
 <0 on error */
static inline int iso_ent_write(void *buf, ffsize cap, const struct iso_file *f, ffuint flags)
{
	ffuint fnlen, reserved, rrlen;
	FF_ASSERT(f->name.len != 0);

	reserved = 0;
	if ((f->attr & ISO_FILE_DIR)
		&& f->name.len == 1
		&& (f->name.ptr[0] == 0x00 || f->name.ptr[0] == 0x01))
		reserved = 1;

	// determine filename length
	if (reserved) {
		fnlen = 1;
	} else if (flags & ENT_WRITE_JLT) {
		// Note: by spec these chars are not supported: 0x00..0x1f, * / \\ : ; ?
		ffssize ss = ffutf8_to_utf16(NULL, 0, f->name.ptr, f->name.len, FFUNICODE_UTF16BE);
		if (ss < 0)
			return -ISO_ELARGE; // can't encode into UTF-16
		fnlen = ss;

	} else {
		fnlen = iso_ent_name_write(NULL, &f->name, f->attr);
	}

	// get RR extensions length
	rrlen = 0;
	if (flags & ENT_WRITE_RR) {
		rrlen = sizeof(struct iso_rr) + sizeof(struct iso_rr_rr);
		if (!reserved)
			rrlen += sizeof(struct iso_rr) + sizeof(struct iso_rr_nm) + f->name.len;
		if (flags & ENT_WRITE_RR_SP)
			rrlen += sizeof(struct iso_rr) + sizeof(struct iso_rr_sp);
	}

	if (iso_ent_len2(fnlen) + rrlen > 255)
		return -ISO_ELARGE;

	if (buf == NULL)
		return iso_ent_len2(fnlen) + rrlen;

	if (cap < iso_ent_len2(fnlen) + rrlen)
		return -ISO_ELARGE;

	struct iso_fileentry *ent = (struct iso_fileentry*)buf;
	iso_write16(ent->vol_seqnum, 1);
	ent->namelen = fnlen;
	ent->len = iso_ent_len(ent) + rrlen;
	iso_write32(ent->body_off, f->off / ISO_SECTOR);
	iso_write32(ent->body_len, f->size);

	ffdatetime dt;
	fftime t = f->mtime;
	t.sec += FFTIME_1970_SECONDS;
	fftime_split1(&dt, &t);
	iso_date_write(&ent->date, &dt);

	if (f->attr & ISO_FILE_DIR)
		ent->flags = ISO_FDIR;

	// write filename
	if (reserved) {
		ent->name[0] = f->name.ptr[0];
	} else if (flags & ENT_WRITE_JLT) {
		ffutf8_to_utf16((char*)ent->name, 255, f->name.ptr, f->name.len, FFUNICODE_UTF16BE);
	} else {
		iso_ent_name_write((char*)ent->name, &f->name, f->attr);
	}

	// write RR extensions
	if (rrlen != 0) {
		char *p = (char*)buf + iso_ent_len(ent);

		if (flags & ENT_WRITE_RR_SP) {
			p += iso_rr_write(p, "SP", sizeof(struct iso_rr_sp));
			struct iso_rr_sp *sp = (struct iso_rr_sp*)p;
			sp->data[0] = 0xbe;
			sp->data[1] = 0xef;
			p += sizeof(struct iso_rr_sp);
		}

		p += iso_rr_write(p, "RR", sizeof(struct iso_rr_rr));
		struct iso_rr_rr *r = (struct iso_rr_rr*)p;
		p += sizeof(struct iso_rr_rr);

		if (!reserved) {
			p += iso_rr_write(p, "NM", sizeof(struct iso_rr_nm) + f->name.len);
			struct iso_rr_nm *nm = (struct iso_rr_nm*)p;
			ffmem_copy((char*)nm->data, f->name.ptr, f->name.len);
			p += sizeof(struct iso_rr_nm) + f->name.len;
			r->flags |= ISO_RR_HAVE_NM;
		}
	}

	return ent->len;
}


struct iso_pathentry {
	ffbyte len;
	ffbyte unused;
	ffbyte extent[4];
	ffbyte parent_index[2];
	ffbyte name[0];
	// ffbyte pad;
};

enum ISO_PATHENT_WRITE_F {
	ISO_PATHENT_WRITE_BE = 1,
	ISO_PATHENT_WRITE_JLT = 2,
};

/** Write 1 dir in path table
flags: enum ISO_PATHENT_WRITE_F */
static inline int iso_pathentry_write(void *dst, ffsize cap, const ffstr *name, ffuint extent, ffuint parent, ffuint flags)
{
	int reserved;
	ffsize fnlen;

	reserved = (name->len == 1 && name->ptr[0] == 0x00);

	if (reserved)
		fnlen = 1;
	else if (flags & ISO_PATHENT_WRITE_JLT)
		fnlen = name->len * 2;
	else
		fnlen = iso_ent_name_write(NULL, name, ISO_FILE_DIR);
	ffuint n = sizeof(struct iso_pathentry) + fnlen + !!(fnlen % 2);
	if (n > 255)
		return -ISO_ELARGE;
	if (dst == NULL)
		return n;
	if (n > cap)
		return -ISO_ELARGE;

	struct iso_pathentry *p = (struct iso_pathentry*)dst;
	p->len = fnlen;

	if (flags & ISO_PATHENT_WRITE_BE) {
		*(ffuint*)p->extent = ffint_be_cpu32(extent);
		*(ffushort*)p->parent_index = ffint_be_cpu16(parent);
	} else {
		*(ffuint*)p->extent = ffint_le_cpu32(extent);
		*(ffushort*)p->parent_index = ffint_le_cpu16(parent);
	}

	if (reserved) {
		p->name[0] = '\0';
	} else if (flags & ISO_PATHENT_WRITE_JLT) {
		ffutf8_to_utf16((char*)p->name, 255, name->ptr, name->len, FFUNICODE_UTF16BE);
	} else {
		iso_ent_name_write((char*)p->name, name, ISO_FILE_DIR);
	}

	return n;
}

/** ffpack: .tar format
2020, Simon Zolin */

/*
tar_num64 tar_num tar_num_write
tar_checksum
tar_hdr_read tar_hdr_write
*/

/* .tar format:
([HDR(L) LONGNAME_DATA...]  HDR DATA...)...  3*PADDING
where each block = 512B(data [padding=0x00])
*/

#pragma once

#include <ffbase/string.h>
#include <ffbase/time.h>

enum TAR_TYPE {
	TAR_FILE = '0',
	TAR_FILE0 = '\0',
	TAR_HLINK = '1',
	TAR_SLINK = '2',
	TAR_CHAR = '3',
	TAR_BLOCK = '4',
	TAR_DIR = '5',
	TAR_FIFO = '6',
	TAR_LONG = 'L', //the data in this block is the name of the next file
	TAR_EXTHDR = 'g', //global extended header
	TAR_NEXTHDR = 'x', //extended header for the next file
};

struct tar_fileinfo {
	ffuint type; // enum TAR_TYPE
	ffstr name;
	ffstr link_to;
	fftime mtime; // seconds since 1970
	ffuint attr_unix; // UNIX file attributes
	ffuint uid, gid; // UNIX user/group ID
	ffstr user_name, group_name; // UNIX user/group name
	ffuint64 size;
};

struct tar_hdr {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8]; // "123456\0 "
	char typeflag; // enum TAR_TYPE
	char linkname[100];
};

#define TAR_GNU_MAGIC  "ustar  \0"
struct tar_hdr_gnu {
	char magic[8];
	char uname[32];
	char gname[32];
};

#define TAR_USTAR_MAGIC  "ustar\0"
struct tar_hdr_ustar {
	char magic[6];
	char version[2]; // "00"
	char uname[32];
	char gname[32];
	char unused[8];
	char unused2[8];
	char prefix[155]; // file path for long names
};

/** Parse octal number terminated with ' ' or '\0': "..000123.." */
static inline int _tar_num(const char *d, ffsize len, void *dst, ffuint f)
{
	ffstr s = FFSTR_INITN(d, len);
	ffstr skip = FFSTR_INITN(" \0", 2);
	ffstr_skipany(&s, &skip);
	ffstr_rskipany(&s, &skip);
	if (s.len == 0)
		ffstr_setz(&s, "0");
	return !ffstr_toint(&s, dst, FFS_INTOCTAL | f);
}

/** Read tar 64bit number
Large value format: 0x80 0 0 0 (int64)
Return 0 on success */
static inline int tar_num64(const char *d, ffsize len, ffuint64 *dst)
{
	if (d[0] & 0x80) {
		*dst = ffint_be_cpu64_ptr(d + 4);
		return 0;
	}
	return _tar_num(d, len, dst, FFS_INT64);
}

/** Read tar 32bit number
Large value format: 0x80 0 0 0 (int32)
Return 0 on success */
static inline int tar_num(const char *d, ffsize len, ffuint *dst)
{
	if (d[0] & 0x80) {
		*dst = ffint_be_cpu32_ptr(d + 4);
		return 0;
	}
	return _tar_num(d, len, dst, FFS_INT32);
}

/** Write tar number */
static inline int tar_num_write(ffuint64 n, char *dst, ffsize cap)
{
	if (cap == 12 && n > 077777777777ULL) {
		*(ffuint*)dst = ffint_be_cpu32(0x80000000);
		*(ffuint64*)(dst+4) = ffint_be_cpu64(n);

	} else if (cap == 8 && n > 07777777) {
		*(ffuint*)dst = ffint_be_cpu32(0x80000000);
		*(ffuint*)(dst+4) = ffint_be_cpu32(n);

	} else {
		if (0 == ffs_fromint(n, dst, cap, FFS_INTOCTAL | FFS_INTZERO | FFS_INTWIDTH(cap - 1)))
			return 1;
	}
	return 0;
}

/** Get header checksum */
static inline ffuint tar_checksum(const char *d, ffsize len)
{
	ffuint c = 0, i = 0;
	for (;  i != FF_OFF(struct tar_hdr, chksum);  i++) {
		c += (ffbyte)d[i];
	}
	c += ' ' * 8;
	i += 8;
	for (;  i != len;  i++) {
		c += (ffbyte)d[i];
	}
	return c;
}

static const ffbyte tar_ftype[] = {
	0100000 >> 12, // TAR_FILE
	0100000 >> 12, // TAR_HLINK
	0120000 >> 12, // TAR_SLINK
	0020000 >> 12, // TAR_CHAR
	0060000 >> 12, // TAR_BLOCK
	0040000 >> 12, // TAR_DIR
	0010000 >> 12, // TAR_FIFO
};

/** Get NULL-terminated string length */
static inline int _tar_szlen(const char *s, ffsize cap)
{
	int r = ffs_findchar(s, cap, '\0');
	if (r < 0)
		return cap;
	return r;
}

enum TAR_ERR {
	TAR_ENUMBER = 1,
	TAR_ECHECKSUM = 2,
	TAR_EHAVEDATA = 4,
	TAR_ESIZE = 8,
};

/** Read header
buf: 512 bytes
filename: (optional) file name buffer (256 bytes capacity)
Return 0 on success;
  flags(enum TAR_ERR) on error */
static inline int tar_hdr_read(const char *buf, struct tar_fileinfo *f, char *filename)
{
	int rc = 0;
	const struct tar_hdr *h = (struct tar_hdr*)buf;
	int t = h->typeflag;
	f->type = h->typeflag;

	if (filename != NULL) {
		ffuint n = _tar_szlen(h->name, sizeof(h->name));
		ffmem_copy(filename, h->name, n);
		ffstr_set(&f->name, filename, n);
	}

	int e = tar_num(h->mode, sizeof(h->mode), &f->attr_unix);
	f->attr_unix &= 0777;
	if (t >= '0' && t <= '6')
		f->attr_unix |= (ffuint)tar_ftype[t - '0'] << 12;
	else
		f->attr_unix |= 0100000;

	e |= tar_num(h->uid, sizeof(h->uid), &f->uid);
	e |= tar_num(h->gid, sizeof(h->gid), &f->gid);
	if (0 != tar_num64(h->size, sizeof(h->size), &f->size))
		rc |= TAR_ESIZE;

	ffuint64 tmval = 0;
	e |= tar_num64(h->mtime, sizeof(h->mtime), &tmval);
	f->mtime.sec = tmval;  f->mtime.nsec = 0;

	if (e)
		rc |= TAR_ENUMBER;

	switch (t) {
	case TAR_DIR:
	case TAR_HLINK:
	case TAR_SLINK:
		if (f->size != 0)
			rc |= TAR_EHAVEDATA;
		if (t == TAR_SLINK || t == TAR_HLINK) {
			ffstr_set(&f->link_to, h->linkname, _tar_szlen(h->linkname, sizeof(h->linkname)));
		}
		break;
	}

	if (!ffmem_cmp(h + 1, TAR_GNU_MAGIC, FFS_LEN(TAR_GNU_MAGIC))) {
		const struct tar_hdr_gnu *g = (struct tar_hdr_gnu*)(h + 1);
		ffstr_set(&f->user_name, g->uname, _tar_szlen(g->uname, sizeof(g->uname)));
		ffstr_set(&f->group_name, g->gname, _tar_szlen(g->gname, sizeof(g->gname)));

	} else if (!ffmem_cmp(h + 1, TAR_USTAR_MAGIC "00", FFS_LEN(TAR_USTAR_MAGIC "00"))) {
		const struct tar_hdr_ustar *us = (struct tar_hdr_ustar*)(h + 1);
		ffstr_set(&f->user_name, us->uname, _tar_szlen(us->uname, sizeof(us->uname)));
		ffstr_set(&f->group_name, us->gname, _tar_szlen(us->gname, sizeof(us->gname)));
		if (filename != NULL && us->prefix[0] != '\0') {
			int n = ffs_format(filename, -1, "%*s/%*s"
				, _tar_szlen(us->prefix, sizeof(us->prefix)), us->prefix
				, _tar_szlen(h->name, sizeof(h->name)), h->name);
			ffstr_set(&f->name, filename, n);
		}
	}

	ffuint hchk, chk = tar_checksum((void*)h, 512);
	if (0 != tar_num(h->chksum, sizeof(h->chksum), &hchk)
		|| hchk != chk)
		rc |= TAR_ECHECKSUM;

	return rc;
}

/** Write 1 header */
static inline int _tar_hdr_write(char *buf, const struct tar_fileinfo *f, ffuint type)
{
	struct tar_hdr *h = (struct tar_hdr*)buf;

	h->typeflag = type;
	ffmem_copy(h->name, f->name.ptr, ffmin(f->name.len, sizeof(h->name)));

	tar_num_write(f->attr_unix & 0777, h->mode, sizeof(h->mode));
	ffuint e = tar_num_write(f->uid, h->uid, sizeof(h->uid));
	e |= tar_num_write(f->gid, h->gid, sizeof(h->gid));

	int dir = (f->attr_unix & 0170000) == 0040000;
	if (dir)
		tar_num_write(0, h->size, sizeof(h->size));
	else
		e |= tar_num_write(f->size, h->size, sizeof(h->size));

	e |= tar_num_write(f->mtime.sec, h->mtime, sizeof(h->mtime));

	if (e)
		return TAR_ENUMBER;

	if (f->link_to.len > sizeof(h->linkname))
		return TAR_ESIZE;
	ffmem_copy(h->linkname, f->link_to.ptr, f->link_to.len);

	struct tar_hdr_gnu *g = (struct tar_hdr_gnu*)(h + 1);
	ffmem_copy(g->magic, TAR_GNU_MAGIC, FFS_LEN(TAR_GNU_MAGIC));

	ffstr s;
	ffstr_setz(&s, "root");
	if (f->user_name.len != 0)
		s = f->user_name;
	ffmem_copy(g->uname, s.ptr, ffmin(s.len, sizeof(g->uname)));

	ffstr_setz(&s, "root");
	if (f->group_name.len != 0)
		s = f->group_name;
	ffmem_copy(g->gname, s.ptr, ffmin(s.len, sizeof(g->gname)));

	ffuint chksum = tar_checksum(buf, sizeof(struct tar_hdr) + sizeof(struct tar_hdr_gnu));
	tar_num_write(chksum, h->chksum, sizeof(h->chksum) - 2);
	h->chksum[sizeof(h->chksum) - 2] = '\0';
	return 0;
}

/** Write header.  Supports long names.
buf: NULL: return buffer capacity needed
Return
  >0: number of bytes written;
  <0: error */
static inline int tar_hdr_write(char *buf, const struct tar_fileinfo *f)
{
	int r;
	ffsize cap = 512;
	if (f->name.len > 100)
		cap = 512 + ffint_align_ceil2(f->name.len, 512) + 512;
	if (buf == NULL)
		return cap;

	ffmem_zero(buf, cap);

	if (f->name.len > 100) {
		struct tar_fileinfo fl = {};
		ffstr_setz(&fl.name, "././@LongLink");
		fl.attr_unix = 0644;
		fl.size = f->name.len;
		fl.mtime.sec = 0;  fl.mtime.nsec = 0;

		if (0 != (r = _tar_hdr_write(buf, &fl, TAR_LONG)))
			return -r;
		buf += 512;

		ffmem_copy(buf, f->name.ptr, f->name.len);
		buf += ffint_align_ceil2(f->name.len, 512);
	}

	ffuint t = f->attr_unix & 0170000;
	ffuint type = TAR_FILE;
	for (ffuint i = 0;  i != 7;  i++) {
		if (t == ((ffuint)tar_ftype[i] << 12)) {
			type = TAR_FILE + i;
			break;
		}
	}
	if (0 != (r = _tar_hdr_write(buf, f, type)))
		return -r;
	return cap;
}

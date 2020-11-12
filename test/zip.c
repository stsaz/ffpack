/** ffpack: zip.h tester
2020, Simon Zolin */

#include <ffpack/zipread.h>
#include <ffpack/zipwrite.h>
#include <test/test.h>

#define fflog(fmt, ...)  (void) printf(fmt "\n", ##__VA_ARGS__)

struct member {
	const char *name;
	ffuint osize;
	ffuint mtime;
	ffuint attr_win, attr_unix;
	ffuint compress_method;
	ffuint uid, gid;
	ffuint offset;
};
static struct member members[] = {
	{ "file-deflated", 10, 1234567890, 0x20,0100000, ZIP_DEFLATED, 1,2, 0, },
	{ "file-stored", 10, 1234567890, 0x20,0100000, ZIP_STORED, 1,2, 0, },
	{ "file-empty", 0, 1234567891, 0x20,0100000, ZIP_STORED, 1,2, 0, },
	{ "dir/", 0, 1234567892, 0x10,0040000, ZIP_STORED, 1,2, 0, },
};

static char* plaindata[] = { "plain ", "data" };

void test_zip_write(ffvec *buf, ffuint non_seekable)
{
	ffstr plain = {}, zipdata;
	ffzipwrite w = {};
	w.non_seekable = non_seekable;
	int next_file = 1;
	int ifile = 0;
	ffuint offset = 0;

	ffuint chunk = 0;
	for (;;) {

		if (next_file) {
			next_file = 0;
			if (ifile == FF_COUNT(members)) {
				ffzipwrite_finish(&w);
				continue;
			}

			const struct member *m = &members[ifile];
			ffzipwrite_conf conf = {};
			ffstr_setz(&conf.name, m->name);
			conf.mtime.sec = m->mtime;
			conf.attr_win = m->attr_win;
			conf.attr_unix = m->attr_unix;
			conf.compress_method = m->compress_method;
			conf.uid = m->uid;
			conf.gid = m->gid;
			x(0 == ffzipwrite_fileadd(&w, &conf));
		}

		int r = ffzipwrite_process(&w, &plain, &zipdata);
		// fflog("ffzipwrite_process: %d", r);
		switch (r) {
		case FFZIPWRITE_DATA:
			if (offset == buf->len)
				ffvec_add2T(buf, &zipdata, char);
			else
				ffmem_copy((char*)buf->ptr + offset, zipdata.ptr, zipdata.len);
			offset += zipdata.len;
			break;

		case FFZIPWRITE_MORE: {
			const struct member *m = &members[ifile];
			if (m->osize != 0) {
				if (chunk < 2) {
					ffstr_setz(&plain, plaindata[chunk]);
					chunk++;
				} else if (chunk == 2) {
					chunk++;
					ffzipwrite_filefinish(&w);
				} else {
					x(0);
				}
			} else {
				x(chunk == 0);
				chunk++;
				ffzipwrite_filefinish(&w);
			}
			break;
		}

		case FFZIPWRITE_SEEK:
			offset = ffzipwrite_offset(&w);
			break;

		case FFZIPWRITE_FILEDONE: {
			const struct member *m = &members[ifile];
			if (m->osize != 0) {
				xieq(3, chunk);
				xieq(ffsz_len(plaindata[0]) + ffsz_len(plaindata[1]), w.file_rd);
			} else {
				xieq(1, chunk);
				xieq(0, w.file_rd);
			}
			chunk = 0;
			ifile++;
			next_file = 1;
			break;
		}

		case FFZIPWRITE_DONE:
			goto done;

		default:
			fflog("error: %s", ffzipwrite_error(&w));
			x(0);
		}
	}

done:
	x(ifile == FF_COUNT(members));
	ffzipwrite_destroy(&w);
}

void test_zip_read(const ffvec *buf)
{
	struct member *m;
	int hdr = 1;
	int ifile = 0;
	ffvec uncomp = {};
	ffzipread r = {};
	x(0 == ffzipread_open(&r, buf->len));
	ffstr in, out;
	ffstr_set(&in, buf->ptr, 0);

	for (;;) {
		int rc = ffzipread_process(&r, &in, &out);

		switch (rc) {
		case FFZIPREAD_MORE:
			if (in.ptr == ffslice_endT(buf, char))
				x(0);
			// in.len = (char*)buf->ptr + buf->len - in.ptr;
			in.len = 1;
			break;

		case FFZIPREAD_FILEINFO: {
			m = &members[ifile];
			const ffzipread_fileinfo_t *info = ffzipread_fileinfo(&r);
			xseq(&info->name, m->name);
			xieq(m->mtime, info->mtime.sec);
			xieq(m->compress_method, info->compress_method);
			xieq(m->uid, info->uid);
			xieq(m->gid, info->gid);
			m->offset = info->hdr_offset;
			xieq(m->osize, info->uncompressed_size);
			ifile++;
			break;
		}

		case FFZIPREAD_FILEHEADER:
			m = &members[ifile];
			const ffzipread_fileinfo_t *info = ffzipread_fileinfo(&r);
			xseq(&info->name, m->name);
			xieq(m->mtime, info->mtime.sec);
			xieq(m->compress_method, info->compress_method);
			xieq(m->uid, info->uid);
			xieq(m->gid, info->gid);
			break;

		case FFZIPREAD_DATA:
			ffvec_add2T(&uncomp, &out, char);
			break;

		case FFZIPREAD_SEEK:
			x(ffzipread_offset(&r) < buf->len);
			in.ptr = (char*)buf->ptr + ffzipread_offset(&r);
			in.len = 1;
			break;

		case FFZIPREAD_DONE:
			m = &members[ifile];
			if (hdr) {
				hdr = 0;
				xieq(FF_COUNT(members), ifile);
				ifile = 0;
			} else {
				if (m->osize != 0) {
					x(ffvec_eqT(&uncomp, "plain data", 10, char));
				} else {
					xieq(0, uncomp.len);
				}
				ffvec_free(&uncomp);
				ifile++;
				if (ifile == FF_COUNT(members)) {
					goto done;
				}
			}

			m = &members[ifile];
			ffzipread_fileread(&r, m->offset, (m->compress_method == ZIP_STORED) ? m->osize : 0);
			break;

		default:
			fflog("error: %s", ffzipread_error(&r));
			x(0);
		}
	}

done:
	ffzipread_close(&r);
	ffvec_free(&uncomp);
}

void test_zip()
{
	ffvec buf = {};
	ffvec_alloc(&buf, 4096, 1);

	test_zip_write(&buf, 0);
	test_zip_read(&buf);
	buf.len = 0;

	test_zip_write(&buf, 1);
	test_zip_read(&buf);
	buf.len = 0;

	ffvec_free(&buf);
}

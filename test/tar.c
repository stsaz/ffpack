/** ffpack: tar.h tester
2020, Simon Zolin */

#include <ffpack/tarread.h>
#include <ffpack/tarwrite.h>
#include <test/test.h>
#include <ffbase/mem-print.h>

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
	{ "file-stored", 10, 1234567890, 0x20,0100644, 0, 1,2, 0, },
	{ "file-longname1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
		, 0, 1234567891, 0x20,0100644, 0, 1,2, 0, },
	{ "file-empty", 0, 1234567892, 0x20,0100644, 0, 1,2, 0, },
	{ "dir/", 0, 1234567893, 0x10,0040000, 0, 1,2, 0, },
};

static char* plaindata[] = { "plain ", "data" };

void test_tar_write(ffvec *buf)
{
	ffstr plain = {}, tardata;
	fftarwrite w = {};
	int next_file = 1;
	int ifile = 0;
	ffuint offset = 0;

	fftarwrite_init(&w);

	ffuint chunk = 0;
	for (;;) {

		if (next_file) {
			next_file = 0;
			if (ifile == FF_COUNT(members)) {
				fftarwrite_finish(&w);
				continue;
			}

			const struct member *m = &members[ifile];
			fftarwrite_conf conf = {};
			ffstr_setz(&conf.name, m->name);
			conf.mtime.sec = m->mtime;
			conf.attr_unix = m->attr_unix;
			conf.uid = m->uid;
			conf.gid = m->gid;
			conf.size = m->osize;
			x(0 == fftarwrite_fileadd(&w, &conf));
		}

		int r = fftarwrite_process(&w, &plain, &tardata);
		// fflog("fftarwrite_process: %d", r);
		switch (r) {
		case FFTARWRITE_DATA:
			if (offset == buf->len)
				ffvec_add2T(buf, &tardata, char);
			else
				ffmem_copy((char*)buf->ptr + offset, tardata.ptr, tardata.len);
			offset += tardata.len;
			break;

		case FFTARWRITE_MORE: {
			const struct member *m = &members[ifile];
			if (m->osize != 0) {
				if (chunk < 2) {
					ffstr_setz(&plain, plaindata[chunk]);
					chunk++;
				} else if (chunk == 2) {
					chunk++;
					fftarwrite_filefinish(&w);
				} else {
					x(0);
				}
			} else {
				x(chunk == 0);
				chunk++;
				fftarwrite_filefinish(&w);
			}
			break;
		}

		case FFTARWRITE_FILEDONE: {
			const struct member *m = &members[ifile];
			if (m->osize != 0) {
				xieq(3, chunk);
			} else {
				xieq(1, chunk);
			}
			chunk = 0;
			ifile++;
			next_file = 1;
			break;
		}

		case FFTARWRITE_DONE:
			goto done;

		default:
			fflog("error: %s", fftarwrite_error(&w));
			x(0);
		}
	}

done:
	x(ifile == FF_COUNT(members));
	fftarwrite_destroy(&w);
}

void test_tar_read(const ffvec *buf)
{
	struct member *m;
	int ifile = 0;
	ffvec uncomp = {};
	fftarread r = {};
	x(0 == fftarread_open(&r));
	ffstr in, out;
	ffstr_set(&in, buf->ptr, 0);

	for (;;) {
		int rc = fftarread_process(&r, &in, &out);

		switch (rc) {
		case FFTARREAD_MORE:
			if (in.ptr == ffslice_endT(buf, char))
				x(0);
			// in.len = (char*)buf->ptr + buf->len - in.ptr;
			in.len = 1;
			break;

		case FFTARREAD_FILEHEADER:
			m = &members[ifile];
			const fftarread_fileinfo_t *info = fftarread_fileinfo(&r);
			xseq(&info->name, m->name);
			xieq(m->mtime, info->mtime.sec);
			xieq(m->uid, info->uid);
			xieq(m->gid, info->gid);
			break;

		case FFTARREAD_DATA:
			ffvec_add2T(&uncomp, &out, char);
			break;

		case FFTARREAD_FILEDONE:
			m = &members[ifile];
			if (m->osize != 0) {
				x(ffvec_eqT(&uncomp, "plain data", 10, char));
			} else {
				xieq(0, uncomp.len);
			}
			ffvec_free(&uncomp);
			ifile++;
			break;

		case FFTARREAD_DONE:
			x(ifile == FF_COUNT(members));
			goto done;

		default:
			fflog("error: %s", fftarread_error(&r));
			x(0);
		}
	}

done:
	fftarread_close(&r);
	ffvec_free(&uncomp);
}

void test_tar()
{
	ffvec buf = {};
	ffvec_alloc(&buf, 4096, 1);

	test_tar_write(&buf);

#if 0
	ffstr s = ffmem_print(buf.ptr, buf.len, FFMEM_PRINT_ZEROSPACE);
	fflog("%.*s", (int)s.len, s.ptr);
	ffstr_free(&s);
#endif

#if 0
	write(1, buf.ptr, buf.len);
#endif

	test_tar_read(&buf);
	buf.len = 0;

	ffvec_free(&buf);
}

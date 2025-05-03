/** ffpack: gz.h tester
2020, Simon Zolin */

#include <ffpack/gz-read.h>
#include <ffpack/gz-write.h>
#include <test/test.h>

#define fflog(fmt, ...)  (void) printf(fmt "\n", ##__VA_ARGS__)

static char* plaindata[] = { "plain ", "data" };

void test_gz_write(ffvec *buf)
{
	ffstr plain = {}, gzdata;
	ffgzwrite w = {};
	ffgzwrite_conf conf = {};
	ffstr_setz(&conf.name, "file-name");
	ffstr_setz(&conf.comment, "comment");
	conf.mtime = 1234;
	ffgzwrite_init(&w, &conf);

	ffuint i = 0;
	for (;;) {
		int r = ffgzwrite_process(&w, &plain, &gzdata);
		switch (r) {
		case FFGZWRITE_DATA:
			ffvec_add2T(buf, &gzdata, char);
			break;

		case FFGZWRITE_MORE:
			if (i < 2) {
				ffstr_setz(&plain, plaindata[i]);
				i++;
			} else if (i == 2) {
				i++;
				ffgzwrite_finish(&w);
			} else {
				x(0);
			}
			break;

		case FFGZWRITE_DONE:
			goto done;

		default:
			fflog("error: %s", ffgzwrite_error(&w));
			x(0);
		}
	}

done:
	xieq(ffsz_len(plaindata[0]) + ffsz_len(plaindata[1]), w.total_rd);
	ffgzwrite_destroy(&w);
}

void test_gz_read(const ffvec *buf, ffint64 total_size)
{
	ffvec uncomp = {};
	ffgzread r = {};
	x(0 == ffgzread_open(&r, total_size));
	ffstr in, out;
	in.ptr = buf->ptr;
	in.len = 1;
	for (;;) {
		int rc = ffgzread_process(&r, &in, &out);
		switch (rc) {
		case FFGZREAD_MORE:
			if (in.ptr == ffslice_endT(buf, char))
				x(0);
			in.len = 1;
			break;

		case FFGZREAD_INFO: {
			const ffgzread_info *info = ffgzread_getinfo(&r);
			xseq(&info->name, "file-name");
			xseq(&info->comment, "comment");
			if (total_size >= 0)
				xieq(10, info->uncompressed_size);
			xieq(1234, info->mtime);
			break;
		}

		case FFGZREAD_DATA:
			ffvec_add2T(&uncomp, &out, char);
			break;

		case FFGZREAD_SEEK:
			x(ffgzread_offset(&r) < buf->len);
			in.ptr = (char*)buf->ptr + ffgzread_offset(&r);
			in.len = 1;
			break;

		case FFGZREAD_DONE:
			if (in.ptr != ffslice_endT(buf, char)) {
				ffgzread_close(&r);
				total_size = -1;
				x(0 == ffgzread_open(&r, -1));
				continue;
			}
			goto done;

		default:
			fflog("error: %s", ffgzread_error(&r));
			x(0);
		}
	}

done:
	ffgzread_close(&r);
	x(ffvec_eqT(&uncomp, "plain dataplain data", 20, char));
	ffvec_free(&uncomp);
}

void test_gz()
{
	ffvec buf = {};
	ffvec_alloc(&buf, 4096, 1);
	test_gz_write(&buf);
	test_gz_write(&buf);
	test_gz_read(&buf, -1);
	test_gz_read(&buf, buf.len);
	ffvec_free(&buf);
}

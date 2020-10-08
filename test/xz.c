/** ffpack: xz.h tester
2020, Simon Zolin */

#include <ffpack/xzread.h>
#include <test/test.h>

#define fflog(fmt, ...)  (void) printf(fmt "\n", ##__VA_ARGS__)

const ffbyte xzdata[] = {
	0xfd,0x37,0x7a,0x58,0x5a,0x00,0x00,0x04,0xe6,0xd6,0xb4,0x46,0x02,0x00,0x21,0x01,
	0x16,0x00,0x00,0x00,0x74,0x2f,0xe5,0xa3,0x01,0x00,0x09,0x70,0x6c,0x61,0x69,0x6e,
	0x20,0x64,0x61,0x74,0x61,0x00,0x00,0x00,0x88,0x6c,0x7e,0xf1,0xa6,0xf5,0x65,0x47,
	0x00,0x01,0x22,0x0a,0x15,0x1a,0xe1,0x67,0x1f,0xb6,0xf3,0x7d,0x01,0x00,0x00,0x00,
	0x00,0x04,0x59,0x5a,
};

void test_xz_read(const ffvec *buf, ffint64 total_size)
{
	ffvec uncomp = {};
	ffxzread r = {};
	x(0 == ffxzread_open(&r, total_size));
	ffstr in, out;
	in.ptr = buf->ptr;
	in.len = 1;
	for (;;) {
		int rc = ffxzread_process(&r, &in, &out);
		switch (rc) {
		case FFXZREAD_MORE:
			if (in.ptr == ffslice_endT(buf, char))
				x(0);
			in.len = 1;
			break;

		case FFXZREAD_INFO: {
			const ffxzread_info *info = ffxzread_getinfo(&r);
			if (total_size >= 0)
				xieq(10, info->uncompressed_size);
			break;
		}

		case FFXZREAD_DATA:
			ffvec_add2T(&uncomp, &out, char);
			break;

		case FFXZREAD_SEEK:
			x(ffxzread_offset(&r) < buf->len);
			in.ptr = (char*)buf->ptr + ffxzread_offset(&r);
			in.len = 1;
			break;

		case FFXZREAD_DONE:
			if (in.ptr != ffslice_endT(buf, char)) {
				ffxzread_close(&r);
				total_size = -1;
				x(0 == ffxzread_open(&r, -1));
				continue;
			}
			goto done;

		default:
			fflog("error: %s", ffxzread_error(&r));
			x(0);
		}
	}

done:
	ffxzread_close(&r);
	x(!ffmem_cmp(uncomp.ptr, "plain data", 10));
	ffvec_free(&uncomp);
}

void test_xz()
{
	ffvec buf = {};
	ffvec_alloc(&buf, 4096, 1);
	ffvec_addT(&buf, xzdata, sizeof(xzdata), char);
	// ffvec_addT(&buf, xzdata, sizeof(xzdata), char);
	// test_xz_read(&buf, -1);
	test_xz_read(&buf, buf.len);
	ffvec_free(&buf);
}

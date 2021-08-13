/** ffpack: zstd-ff.h tester
2020, Simon Zolin */

#include <zstd/zstd-ff.h>
#include <ffbase/vector.h>
#include <test/test.h>

void test_zstd_encode(ffstr *in, ffvec *out)
{
	int partial = 2;
	zstd_encoder *z = NULL;
	zstd_enc_conf conf = {};
	// conf.comp_level = 6;
	// conf.workers = 2;
	zstd_encode_init(&z, &conf);
	uint f = 0;
	for (int k = in->len;  ;  k--) {
		x(k >= 0);
		zstd_buf zin, zout;
		zstd_buf_set(&zin, in->ptr, ffmin(in->len, partial));
		zstd_buf_set(&zout, &((char*)out->ptr)[out->len], ffvec_unused(out));
		int r = zstd_encode(z, &zin, &zout, f);
		ffstr_shift(in, zin.pos);
		out->len += zout.pos;
		x(r >= 0);
		if (zout.pos == 0) {
			if (f == ZSTD_FFINISH)
				break;
			if (in->len == 0)
				f = ZSTD_FFINISH;
		}
	}
	zstd_encode_free(z);
}

void test_zstd_decode(ffstr *in, ffvec *out)
{
	int partial = 2;
	zstd_decoder *z = NULL;
	zstd_dec_conf conf = {};
	// conf.max_mem_kb = 1;
	zstd_decode_init(&z, &conf);
	for (int k = in->len;  ;  k--) {
		x(k >= 0);
		zstd_buf zin, zout;
		zstd_buf_set(&zin, in->ptr, ffmin(in->len, partial));
		zstd_buf_set(&zout, &((char*)out->ptr)[out->len], ffvec_unused(out));
		int r = zstd_decode(z, &zin, &zout);
		ffstr_shift(in, zin.pos);
		out->len += zout.pos;
		x(r >= 0);
		if (zout.pos == 0 && in->len == 0)
			break;
	}
	zstd_decode_free(z);
}

void test_zstd()
{
	ffstr in;
	ffstr_setz(&in, "hello friend");

	ffvec out = {};
	ffvec_alloc(&out, 64, 1);
	test_zstd_encode(&in, &out);

	ffvec out2 = {};
	ffvec_alloc(&out2, 64, 1);
	// in.len--;
	test_zstd_decode(&in, &out2);
	x(ffstr_eq2((ffstr*)&out2, &in));

	ffvec_free(&out);
	ffvec_free(&out2);
}

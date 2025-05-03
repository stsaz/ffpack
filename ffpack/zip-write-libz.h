/** ffpack: .zip libz encode filter
2020, Simon Zolin */

#include <zlib/zlib-ff.h>

static void* _ffzipw_deflated_open(ffzipwrite *w, ffzipwrite_conf *conf)
{
	z_ctx *p;
	// 	z_deflate_reset(w->filter_obj);
	z_conf zconf = {};
	zconf.level = conf->deflate_level;
	zconf.mem = conf->deflate_mem;
	if (0 != z_deflate_init(&p, &zconf)) {
		w->error = "z_deflate_init()";
		return NULL;
	}
	return p;
}

static void _ffzipw_deflated_close(void *obj, ffzipwrite *w)
{
	(void)w;
	if (obj == NULL) return;

	z_deflate_free(obj);
}

static int _ffzipw_deflated_pack(void *obj, ffzipwrite *w, ffstr *input, ffstr *output)
{
	ffsize rd = input->len;
	ffuint flags = (w->file_fin) ? Z_FINISH : 0;
	int r = z_deflate(obj, input->ptr, &rd, (char*)w->buf.ptr, w->buf.cap, flags);
	ffstr_shift(input, rd);

	if (r == Z_DONE) {
		return 0xa11;

	} else if (r < 0) {
		w->error = "z_deflate";
		return 0xbad;

	} else if (r == 0) {
		return 0xfeed;
	}

	w->buf.len += r;
	ffstr_set2(output, &w->buf);
	w->buf.len = 0;
	return 0;
}

static const ffzipwrite_filter _ffzipw_deflated = {
	_ffzipw_deflated_open, _ffzipw_deflated_close, _ffzipw_deflated_pack
};

/** ffpack: .iso tester
2021, Simon Zolin */

#include <ffpack/isoread.h>
#include <ffpack/isowrite.h>
#include <test/test.h>

#define fflog(fmt, ...)  (void) printf(fmt "\n", ##__VA_ARGS__)

struct file {
	const char *name;
	ffuint attr;
	const char *data;
};

static struct file contents[] = {
	{ "afile.txt", 0, "data-afile" },
	{ "mydirectory", ISO_FILE_DIR, "" },
	{ "zfilename.txt", 0, "data-zfilename" },
	{ "mydirectory/file3.txt", 0, "data-file3" },
};

static void test_iso_write(ffvec *data)
{
	ffisowrite o = {};
	ffuint64 off;
	int r;
	ffstr in, out;

	xieq(0, ffisowrite_create(&o, "test", 0));

	// prepare meta
	for (int ifile = 0;  ifile != FF_COUNT(contents);  ifile++) {
		ffisowrite_fileinfo_t f = {};
		struct file *ifi = &contents[ifile];
		ffstr_setz(&f.name, ifi->name);
		f.attr = ifi->attr;
		f.size = ffsz_len(ifi->data);
		xieq(0, ffisowrite_fileadd(&o, &f));
	}

	// write meta
	for (;;) {
		r = ffisowrite_process(&o, NULL, &out);
		if (r == FFISOWRITE_DATA) {
			ffvec_add(data, out.ptr, out.len, 1);
			continue;
		}
		xieq(FFISOWRITE_MORE, r);
		break;
	}

	// write data
	for (int ifile = 0;  ifile != FF_COUNT(contents);  ifile++) {
		struct file *ifi = &contents[ifile];
		if (ifi->attr & ISO_FILE_DIR)
			continue;
		ffisowrite_filenext(&o);
		ffstr_setz(&in, ifi->data);
		for (;;) {
			r = ffisowrite_process(&o, &in, &out);
			if (r == FFISOWRITE_DATA) {
				ffvec_add(data, out.ptr, out.len, 1);
				continue;
			}
			xieq(FFISOWRITE_MORE, r);
			break;
		}
	}

	// finalize
	ffisowrite_finish(&o);
	r = ffisowrite_process(&o, NULL, &out);
	xieq(FFISOWRITE_SEEK, r);
	off = ffisowrite_offset(&o);
	for (;;) {
		r = ffisowrite_process(&o, NULL, &out);
		if (r == FFISOWRITE_DATA) {
			ffmem_copy(data->ptr + off, out.ptr, out.len);
			off += out.len;
			continue;
		}
		xieq(FFISOWRITE_DONE, r);
		break;
	}

	ffisowrite_close(&o);
}

static void test_iso_read(ffvec *data, ffuint opt)
{
	int r;
	ffisoread_fileinfo_t *pf;
	int ifile = 0;
	struct file *ifi;
	ffstr in, out;

	ffisoread iso = {};
	iso.options = opt;
	ffisoread_init(&iso);

	for (;;) {
		r = ffisoread_process(&iso, &in, &out);
		switch (r) {
		case FFISOREAD_MORE:
			ffstr_set2(&in, data);
			break;

		case FFISOREAD_SEEK:
			ffstr_set2(&in, data);
			ffstr_shift(&in, ffisoread_offset(&iso));
			break;

		case FFISOREAD_HDR:
			break;

		case FFISOREAD_FILEMETA:
			ifi = &contents[ifile++];
			pf = ffisoread_fileinfo(&iso);
			x(ffstr_eqz(&pf->name, ifi->name));
			if (!(pf->attr & ISO_FILE_DIR))
				xieq(pf->size, ffsz_len(ifi->data));
			x(pf->attr == ifi->attr);
			ffisoread_storefile(&iso);
			break;

		case FFISOREAD_LISTEND:
			xieq(ifile, FF_COUNT(contents));
			ifile = 0;
			ifi = &contents[ifile];
			pf = ffisoread_nextfile(&iso);
			ffisoread_readfile(&iso, pf);
			x(ffstr_eqz(&pf->name, ifi->name));
			break;

		case FFISOREAD_DATA:
			x(ffstr_eqz(&out, ifi->data));
			break;

		case FFISOREAD_FILEDONE:
			ifile++;
			ifi = &contents[ifile];
			pf = ffisoread_nextfile(&iso);
			if (pf == NULL) {
				xieq(ifile, FF_COUNT(contents));
				goto end;
			}
			ffisoread_readfile(&iso, pf);
			xseq(&pf->name, ifi->name);
			break;

		default:
			x(0);
		}
	}

end:
	ffisoread_close(&iso);
}

void test_iso()
{
	ffvec data = {};

	test_iso_write(&data);
	test_iso_read(&data, FFISOREAD_NO_JOLIET);
	test_iso_read(&data, FFISOREAD_NO_RR);

	ffvec_free(&data);
}

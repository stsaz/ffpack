/** ffpack: tester
2020, Simon Zolin */

#include <ffbase/base.h>
#include <stdio.h>

extern void test_gz();
extern void test_xz();
extern void test_zip();

struct test {
	const char *name;
	void (*func)();
};
#define T(nm) { #nm, &test_ ## nm }
static const struct test atests[] = {
	T(gz),
	T(xz),
	T(zip),
};
#undef T

int main()
{
	const struct test *t;

	//run all tests
	FF_FOREACH(atests, t) {
		printf("%s\n", t->name);
		t->func();
		printf("  OK\n");
	}

	return 0;
}

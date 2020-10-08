/** ffpack: tester
2020, Simon Zolin */

extern void test_gz();
extern void test_xz();

int main()
{
	test_gz();
	test_xz();
	return 0;
}

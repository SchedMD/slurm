/* Test of src/bitstring.c 
 */
#include <stdlib.h>
#include <src/common/bitstring.h>
#include <sys/time.h>
#include <testsuite/dejagnu.h>

/* Test for failure: 
*/
#define TEST(_tst, _msg) do {			\
	if (! (_tst))				\
		fail( _msg );       \
	else					\
		pass( _msg );       \
} while (0)


int
main(int argc, char *argv[])
{
	note("Testing static decl");
	{
		bitstr_t bit_decl(bs, 65);
		/*bitstr_t *bsp = bs;*/

		bit_set(bs,9);
		bit_set(bs,14);
		TEST(bit_test(bs,9), "bit 9 set"); 
		TEST(!bit_test(bs,12), "bit 12 not set");
		TEST(bit_test(bs,14), "bit 14 set" );
		/*bit_free(bsp);*/	/* triggers TEST in bit_free - OK */
	}
	note("Testing basic vixie functions");
	{
		bitstr_t *bs = bit_alloc(16), *bs2;


		/*bit_set(bs, 42);*/ 	/* triggers TEST in bit_set - OK */
		bit_set(bs,9);
		bit_set(bs,14);
		TEST(bit_test(bs,9), "bit 9 set"); 
		TEST(!bit_test(bs,12), "bit 12 not set" );
		TEST(bit_test(bs,14), "bit 14 set");

		bs2 = bit_copy(bs);
		bit_fill_gaps(bs2);
		TEST(bit_ffs(bs2) == 9, "first bit set = 9 ");
		TEST(bit_fls(bs2) == 14, "last bit set = 14");
		TEST(bit_set_count(bs2) == 6, "bitstring");
		TEST(bit_test(bs2,12), "bitstring");
		TEST(bit_super_set(bs,bs2) == 1, "bitstring");
		TEST(bit_super_set(bs2,bs) == 0, "bitstring");

		bit_clear(bs,14);
		TEST(!bit_test(bs,14), "bitstring");

		bit_nclear(bs,9,14);
		TEST(!bit_test(bs,9), "bitstring");
		TEST(!bit_test(bs,12), "bitstring");
		TEST(!bit_test(bs,14), "bitstring");

		bit_nset(bs,9,14);
		TEST(bit_test(bs,9), "bitstring");
		TEST(bit_test(bs,12), "bitstring");
		TEST(bit_test(bs,14), "bitstring");

		TEST(bit_ffs(bs) == 9, "ffs");
		TEST(bit_ffc(bs) == 0, "ffc");
		bit_nset(bs,0,8);
		TEST(bit_ffc(bs) == 15, "ffc");

		bit_free(bs);
		/*bit_set(bs,9); */	/* triggers TEST in bit_set - OK */
	}
	note("Testing and/or/not");
	{
		bitstr_t *bs1 = bit_alloc(128);
		bitstr_t *bs2 = bit_alloc(128);

		bit_set(bs1, 100);
		bit_set(bs1, 104);
		bit_set(bs2, 100);

		bit_and(bs1, bs2);
		TEST(bit_test(bs1, 100), "and");
		TEST(!bit_test(bs1, 104), "and");

		bit_set(bs2, 110);
		bit_set(bs2, 111);
		bit_set(bs2, 112);
		bit_or(bs1, bs2);
		TEST(bit_test(bs1, 100), "or");
		TEST(bit_test(bs1, 110), "or");
		TEST(bit_test(bs1, 111), "or");
		TEST(bit_test(bs1, 112), "or");

		bit_not(bs1);
		TEST(!bit_test(bs1, 100), "not");
		TEST(bit_test(bs1, 12), "not");

		bit_free(bs1);
		bit_free(bs2);
	}

	note("testing bit selection");
	{
		bitstr_t *bs1 = bit_alloc(128), *bs2;
		bit_set(bs1, 21);
		bit_set(bs1, 100);
		bit_fill_gaps(bs1);
		bs2 = bit_pick_cnt(bs1,20);

		if (bs2) {
			TEST(bit_set_count(bs2) == 20, "pick");
			TEST(bit_ffs(bs2) == 21, "pick");
			TEST(bit_fls(bs2) == 40, "pick");
			bit_free(bs2);
		}
		else
			TEST(0, "alloc fail");

		bit_free(bs1);
	}
	note("Testing realloc");
	{
		bitstr_t *bs = bit_alloc(1);

		TEST(bit_ffs(bs) == -1, "bitstring");
		bit_set(bs,0);
		/*bit_set(bs, 1000);*/	/* triggers TEST in bit_set - OK */
		bs = bit_realloc(bs,1048576);
		bit_set(bs,1000);
		bit_set(bs,1048575);
		TEST(bit_test(bs, 0), "bitstring");
		TEST(bit_test(bs, 1000), "bitstring");
		TEST(bit_test(bs, 1048575), "bitstring");
		TEST(bit_set_count(bs) == 3, "bitstring");
		bit_clear(bs,0);
		bit_clear(bs,1000);
		TEST(bit_set_count(bs) == 1, "bitstring");
		TEST(bit_ffs(bs) == 1048575, "bitstring");
		bit_free(bs);
	}
	note("Testing bit_fmt");
	{
		char tmpstr[1024];
		bitstr_t *bs = bit_alloc(1024);

		TEST(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr),bs), ""), "bitstring");
		bit_set(bs,42);
		TEST(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr),bs), "42"), "bitstring");
		bit_set(bs,102);
		TEST(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr),bs), "[42,102]"), "bitstring");
		bit_nset(bs,9,14);
		TEST(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr), bs), 
					"[9-14,42,102]"), "bitstring");
	}

	note("Testing bit_nffc/bit_nffs");
	{
		bitstr_t *bs = bit_alloc(1024);

		bit_set(bs, 2);
		bit_set(bs, 6);
		bit_set(bs, 7);
		bit_nset(bs,12,1018); 

		TEST(bit_nffc(bs, 2) == 0, "bitstring");
		TEST(bit_nffc(bs, 3) == 3, "bitstring");
		TEST(bit_nffc(bs, 4) == 8, "bitstring");
		TEST(bit_nffc(bs, 5) == 1019, "bitstring");
		TEST(bit_nffc(bs, 6) == -1, "bitstring");

		TEST(bit_nffs(bs, 1) == 2, "bitstring");
		TEST(bit_nffs(bs, 2) == 6, "bitstring");
		TEST(bit_nffs(bs, 100) == 12, "bitstring");
		TEST(bit_nffs(bs, 1023) == -1, "bitstring");

		bit_free(bs);
	}

	note("Testing bit_unfmt");
	{
		bitstr_t *bs = bit_alloc(1024);
		bitstr_t *bs2 = bit_alloc(1024);
		char tmpstr[4096];

		bit_set(bs,1);
		bit_set(bs,3);
		bit_set(bs,30);
		bit_nset(bs,42,64);
		bit_nset(bs,97,1000);

		bit_fmt(tmpstr, sizeof(tmpstr), bs);
		TEST(bit_unfmt(bs2, tmpstr) != -1, "bitstring");
		TEST(bit_equal(bs, bs2), "bitstring");
	}

	note("Testing complete!");
	exit(0);
}

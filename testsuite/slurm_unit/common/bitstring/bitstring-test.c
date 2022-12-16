/* Test of src/bitstring.c 
 */
#include <stdlib.h>
#include <src/common/log.h>
#include <src/common/bitstring.h>
#include <sys/time.h>
#include <check.h>

START_TEST(test_basic_vixie_functions)
{
	bitstr_t *bs = bit_alloc(16), *bs2, *bs3 = bit_alloc(16);

	/*bit_set(bs, 42);*/ 	/* triggers TEST in bit_set - OK */
	bit_set(bs,9);
	bit_set(bs,14);
	ck_assert_msg(bit_test(bs,9), "bit 9 set");
	ck_assert_msg(!bit_test(bs,12), "bit 12 not set" );
	ck_assert_msg(bit_test(bs,14), "bit 14 set");

	bs2 = bit_copy(bs);
	bit_fill_gaps(bs2);
	ck_assert_msg(bit_ffs(bs2) == 9, "first bit set = 9 ");
	ck_assert_msg(bit_fls(bs2) == 14, "last bit set = 14");
	ck_assert_msg(bit_set_count(bs2) == 6, "bitstring");
	ck_assert_msg(bit_test(bs2,12), "bitstring");
	ck_assert_msg(bit_super_set(bs,bs2) == 1, "bitstring");
	ck_assert_msg(bit_super_set(bs2,bs) == 0, "bitstring");
	/* bs3 == bit_not(bs) */
	bit_unfmt_hexmask(bs3, "0xBDFF");
	bit_not(bs3);
	ck_assert_msg(bit_super_set(bs,bs3) == 1, "bit_super_set after bit_not");
	ck_assert_msg(bit_super_set(bs3,bs) == 1, "bit_super_set after bit_not");

	bit_clear(bs,14);
	ck_assert_msg(!bit_test(bs,14), "bitstring");

	bit_nclear(bs,9,14);
	ck_assert_msg(!bit_test(bs,9), "bitstring");
	ck_assert_msg(!bit_test(bs,12), "bitstring");
	ck_assert_msg(!bit_test(bs,14), "bitstring");

	bit_nset(bs,9,14);
	ck_assert_msg(bit_test(bs,9), "bitstring");
	ck_assert_msg(bit_test(bs,12), "bitstring");
	ck_assert_msg(bit_test(bs,14), "bitstring");

	ck_assert_msg(bit_ffs(bs) == 9, "ffs");
	ck_assert_msg(bit_ffc(bs) == 0, "ffc");
	bit_nset(bs,0,8);
	ck_assert_msg(bit_ffc(bs) == 15, "ffc");

	bit_free(bs);
	bit_free(bs2);
	bit_free(bs3);
	/*bit_set(bs,9); */	/* triggers TEST in bit_set - OK */
}
END_TEST

START_TEST(test_and_or_not)
{
	bitstr_t *bs1 = bit_alloc(128);
	bitstr_t *bs2 = bit_alloc(128);

	bit_set(bs1, 100);
	bit_set(bs1, 104);
	bit_set(bs2, 100);

	bit_and(bs1, bs2);
	ck_assert_msg(bit_test(bs1, 100), "and");
	ck_assert_msg(!bit_test(bs1, 104), "and");

	bit_set(bs2, 110);
	bit_set(bs2, 111);
	bit_set(bs2, 112);
	bit_or(bs1, bs2);
	ck_assert_msg(bit_test(bs1, 100), "or");
	ck_assert_msg(bit_test(bs1, 110), "or");
	ck_assert_msg(bit_test(bs1, 111), "or");
	ck_assert_msg(bit_test(bs1, 112), "or");

	bit_not(bs1);
	ck_assert_msg(!bit_test(bs1, 100), "not");
	ck_assert_msg(bit_test(bs1, 12), "not");

	bit_free(bs1);
	bit_free(bs2);

	bs1 = bit_alloc(32);
	bs2 = bit_alloc(33);
	bit_set_all(bs2);
	bit_and(bs2, bs1);
	ck_assert_msg(!bit_test(bs2, 31), "diff size and");
	ck_assert_msg(bit_test(bs2, 32), "diff size and");
	bit_clear(bs2, 32);
	bit_not(bs1);
	bit_or(bs2, bs1);
	ck_assert_msg(!bit_test(bs2, 32), "diff size or");

	bit_set_all(bs2);
	bit_set_all(bs1);
	bit_and_not(bs2, bs1);
	ck_assert_msg(!bit_test(bs2, 31), "diff size and_not");
	ck_assert_msg(bit_test(bs2, 32), "diff size and_not");

	bit_set_all(bs2);
	bit_clear(bs2, 32);
	bit_not(bs1);
	bit_or_not(bs2, bs1);
	ck_assert_msg(bit_test(bs2, 31), "diff size and_not");
	ck_assert_msg(!bit_test(bs2, 32), "diff size and_not");

	bit_free(bs1);
	bit_free(bs2);
}
END_TEST

START_TEST(test_bit_selection)
{
	bitstr_t *bs1 = bit_alloc(128), *bs2;
	bit_set(bs1, 21);
	bit_set(bs1, 100);
	bit_fill_gaps(bs1);
	bs2 = bit_pick_cnt(bs1,20);
	if (bs2) {
		ck_assert_msg(bit_set_count(bs2) == 20, "pick");
		ck_assert_msg(bit_ffs(bs2) == 21, "pick");
		ck_assert_msg(bit_fls(bs2) == 40, "pick");
		bit_free(bs2);
	}
	else
		ck_assert_msg(0, "alloc fail");
	bit_free(bs1);

	bs1 = bit_alloc(11);
	bit_nset(bs1, 0, 10);
	bit_not(bs1);
	ck_assert_msg(bit_ffs(bs1) == -1, "pick");
	ck_assert_msg(bit_fls(bs1) == -1, "pick");
	bit_free(bs1);
}
END_TEST

START_TEST(test_realloc)
{
	bitstr_t *bs = bit_alloc(1);

	ck_assert_msg(bit_ffs(bs) == -1, "bitstring");
	bit_set(bs,0);
	/*bit_set(bs, 1000);*/	/* triggers TEST in bit_set - OK */
	bs = bit_realloc(bs,1048576);
	bit_set(bs,1000);
	bit_set(bs,1048575);
	ck_assert_msg(bit_test(bs, 0), "bitstring");
	ck_assert_msg(bit_test(bs, 1000), "bitstring");
	ck_assert_msg(bit_test(bs, 1048575), "bitstring");
	ck_assert_msg(bit_set_count(bs) == 3, "bitstring");
	bit_clear(bs,0);
	bit_clear(bs,1000);
	ck_assert_msg(bit_set_count(bs) == 1, "bitstring");
	ck_assert_msg(bit_ffs(bs) == 1048575, "bitstring");
	bit_free(bs);
}
END_TEST

START_TEST(test_bit_fmt)
{
	char tmpstr[1024];
	bitstr_t *bs = bit_alloc(1024);

	ck_assert_msg(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr),bs), ""), "bitstring");
	bit_set(bs,42);
	ck_assert_msg(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr),bs), "42"), "bitstring");
	bit_set(bs,102);
	ck_assert_msg(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr),bs), "42,102"), "bitstring");
	bit_nset(bs,9,14);
	ck_assert_msg(!strcmp(bit_fmt(tmpstr,sizeof(tmpstr), bs),
				"9-14,42,102"), "bitstring");
}
END_TEST

START_TEST(test_bit_nffc_nffs)
{
	bitstr_t *bs = bit_alloc(1024);

	bit_set(bs, 2);
	bit_set(bs, 6);
	bit_set(bs, 7);
	bit_nset(bs,12,1018);

	ck_assert_msg(bit_nffc(bs, 2) == 0, "bitstring");
	ck_assert_msg(bit_nffc(bs, 3) == 3, "bitstring");
	ck_assert_msg(bit_nffc(bs, 4) == 8, "bitstring");
	ck_assert_msg(bit_nffc(bs, 5) == 1019, "bitstring");
	ck_assert_msg(bit_nffc(bs, 6) == -1, "bitstring");

	ck_assert_msg(bit_nffs(bs, 1) == 2, "bitstring");
	ck_assert_msg(bit_nffs(bs, 2) == 6, "bitstring");
	ck_assert_msg(bit_nffs(bs, 100) == 12, "bitstring");
	ck_assert_msg(bit_nffs(bs, 1023) == -1, "bitstring");

	bit_free(bs);
}
END_TEST

START_TEST(test_bit_equal)
{
	bitstr_t *bs1 = bit_alloc(32);
	bitstr_t *bs2 = bit_alloc(32);
	bit_nset(bs1, 0, 31);
	bit_not(bs2);
	ck_assert_msg(bit_equal(bs1, bs2), "bit_equal");
	ck_assert_msg(bit_equal(bs2, bs1), "bit_equal");
	bit_not(bs2);
	ck_assert_msg(!bit_equal(bs1, bs2), "not bit_equal");
	ck_assert_msg(!bit_equal(bs2, bs1), "not bit_equal");

	bit_free(bs1);
	bit_free(bs2);

	bs1 = bit_alloc(10);
	bs2 = bit_alloc(10);
	bit_set(bs1, 0);
	bit_nset(bs1, 3, 9);
	bit_nset(bs2, 0, 9);
	ck_assert_msg(!bit_equal(bs1, bs2), "not bit_equal");
	bit_free(bs1);
	bit_free(bs2);

	bs1 = bit_alloc(10);
	bs2 = bit_alloc(10);
	bit_not(bs1);
	bit_not(bs2);
	ck_assert_msg(bit_equal(bs1, bs2), "bit_equal");
	ck_assert_msg(bit_equal(bs2, bs1), "bit_equal");
	bit_free(bs1);
	bit_free(bs2);
}
END_TEST

START_TEST(test_bit_unfmt)
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
	ck_assert_msg(bit_unfmt(bs2, tmpstr) != -1, "bitstring");
	ck_assert_msg(bit_equal(bs, bs2), "bitstring");

	bit_free(bs);
	bit_free(bs2);
}
END_TEST

START_TEST(test_bit_overlap)
{
	bitstr_t *bs = bit_alloc(1000);
	bitstr_t *bs2;

	bit_set(bs,1);
	bit_set(bs,3);
	bit_set(bs,64);
	bit_set(bs,998);
	bit_set(bs,999);

	bs2 = bit_copy(bs);
	bit_not(bs2);
	ck_assert_msg(bit_overlap(bs, bs2) == 0, "bitstring");
	ck_assert_msg(bit_overlap_any(bs, bs2) == 0, "bitstring");
	bit_set(bs2,3);
	bit_set(bs2,64);
	bit_set(bs2,999);
	ck_assert_msg(bit_overlap(bs, bs2) == 3, "bitstring");
	ck_assert_msg(bit_overlap_any(bs, bs2) == 1, "bitstring any");

	bit_free(bs);
	bit_free(bs2);
}
END_TEST

START_TEST(test_bit_set_count_range)
{
	bitstr_t *bs = bit_alloc(16);
	bit_nset(bs,0,14);
	ck_assert_msg(bit_set_count_range(bs,0,14) == 14, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,2,14) == 12, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,2,15) == 13, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,2,16) == 13, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,0,15) == 15, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,0,16) == 15, "bit_set_count_range");
	bit_set(bs,15);
	ck_assert_msg(bit_set_count_range(bs,0,16) == 16, "bit_set_count_range");
	bs = bit_realloc(bs,128);
	bit_nset(bs,0,127);
	ck_assert_msg(bit_set_count_range(bs,0,63) == 63, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,0,64) == 64, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,0,65) == 65, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,1,63) == 62, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,1,64) == 63, "bit_set_count_range");
	ck_assert_msg(bit_set_count_range(bs,1,65) == 64, "bit_set_count_range");
	bit_free(bs);
}
END_TEST

START_TEST(test_bit_ffs_from_bit)
{
	bitstr_t *bs = bit_alloc(256);
	bit_set(bs,0);
	bit_set(bs,2);
	bit_set(bs,63);
	bit_set(bs,64);
	ck_assert_msg(bit_ffs_from_bit(bs,0) == 0, "bit_ffs_from_bit");
	ck_assert_msg(bit_ffs_from_bit(bs,1) == 2, "bit_ffs_from_bit");
	ck_assert_msg(bit_ffs_from_bit(bs,2) == 2, "bit_ffs_from_bit");
	ck_assert_msg(bit_ffs_from_bit(bs,3) == 63, "bit_ffs_from_bit");
	ck_assert_msg(bit_ffs_from_bit(bs,63) == 63, "bit_ffs_from_bit");
	ck_assert_msg(bit_ffs_from_bit(bs,64) == 64, "bit_ffs_from_bit");
	ck_assert_msg(bit_ffs_from_bit(bs,65) == -1, "bit_ffs_from_bit");

	bit_clear_all(bs);
	bit_set(bs, 130);
	ck_assert_msg(bit_ffs_from_bit(bs,1) == 130, "bit_ffs_from_bit");

	bit_free(bs);
}
END_TEST

int main(void)
{
	int number_failed;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("pack-test", log_opts, 0, NULL);

	Suite *s = suite_create("pack");
	TCase *tc_core = tcase_create("pack");

	tcase_add_test(tc_core, test_basic_vixie_functions);
	tcase_add_test(tc_core, test_and_or_not);
	tcase_add_test(tc_core, test_bit_selection);
	tcase_add_test(tc_core, test_realloc);
	tcase_add_test(tc_core, test_bit_fmt);
	tcase_add_test(tc_core, test_bit_nffc_nffs);
	tcase_add_test(tc_core, test_bit_equal);
	tcase_add_test(tc_core, test_bit_unfmt);
	tcase_add_test(tc_core, test_bit_overlap);
	tcase_add_test(tc_core, test_bit_set_count_range);
	tcase_add_test(tc_core, test_bit_ffs_from_bit);

	suite_add_tcase(s, tc_core);

	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

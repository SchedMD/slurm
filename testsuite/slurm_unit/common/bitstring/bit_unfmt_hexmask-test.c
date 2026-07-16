#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/bitstring.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

START_TEST(null_test)
{
	bitstr_t *bit_str = bit_alloc(64);
	char *hex_str = NULL;

	ck_assert(bit_unfmt_hexmask(NULL, NULL) == -1);
	ck_assert(bit_unfmt_hexmask(NULL, hex_str) == -1);
	ck_assert(bit_unfmt_hexmask(bit_str, NULL) == -1);

	ck_assert(bit_unfmt_hexmask(bit_str, "Z") == -1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0xZ") == -1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0xZ0") == -1);

	bit_free(bit_str);
}
END_TEST

START_TEST(bounds_test)
{
	bitstr_t *bit_str;

	bit_str = bit_alloc(1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0x2") == -1);
	bit_free(bit_str);

	bit_str = bit_alloc(1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0x4") == -1);
	bit_free(bit_str);

	bit_str = bit_alloc(1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0x8") == -1);
	bit_free(bit_str);

	bit_str = bit_alloc(1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0x10") == -1);
	bit_free(bit_str);

	bit_str = bit_alloc(1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0x20") == -1);
	bit_free(bit_str);

	bit_str = bit_alloc(1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0x40") == -1);
	bit_free(bit_str);

	bit_str = bit_alloc(1);
	ck_assert(bit_unfmt_hexmask(bit_str, "0x80") == -1);
	bit_free(bit_str);
}
END_TEST

START_TEST(good_test)
{
	int rc;
	bitstr_t *bit_str = bit_alloc(64);
	char *out_str;

	rc = bit_unfmt_hexmask(bit_str, "4321");
	ck_assert(rc == 0);
	out_str = bit_fmt_hexmask(bit_str);
	ck_assert(!xstrcmp(out_str, "0x0000000000004321"));
	xfree(out_str);

	rc = bit_unfmt_hexmask(bit_str, "0x4321");
	ck_assert(rc == 0);
	out_str = bit_fmt_hexmask(bit_str);
	ck_assert(!xstrcmp(out_str, "0x0000000000004321"));
	xfree(out_str);

	bit_clear_all(bit_str);
	rc = bit_unfmt_hexmask(bit_str, "0xAbCd");
	ck_assert(rc == 0);
	out_str = bit_fmt_hexmask(bit_str);
	ck_assert(!xstrcmp(out_str, "0x000000000000ABCD"));
	xfree(out_str);

	bit_clear_all(bit_str);
	rc = bit_unfmt_hexmask(bit_str, "0x1248AbCd");
	ck_assert(rc == 0);
	out_str = bit_fmt_hexmask(bit_str);
	ck_assert(!xstrcmp(out_str, "0x000000001248ABCD"));
	xfree(out_str);

	bit_clear_all(bit_str);
	rc = bit_unfmt_hexmask(bit_str, "0x123AbCd");
	ck_assert(rc == 0);
	out_str = bit_fmt_hexmask(bit_str);
	ck_assert(!xstrcmp(out_str, "0x000000000123ABCD"));
	xfree(out_str);

	bit_clear_all(bit_str);
	rc = bit_unfmt_hexmask(bit_str, "0x5555555555155");
	ck_assert(rc == 0);
	out_str = bit_fmt_hexmask(bit_str);
	ck_assert(!xstrcmp(out_str, "0x0005555555555155"));
	xfree(out_str);

	bit_free(bit_str);
	bit_str = bit_alloc(65);

	bit_clear_all(bit_str);
	rc = bit_unfmt_hexmask(bit_str, "0x10000000000000002");
	ck_assert(rc == 0);
	out_str = bit_fmt_hexmask(bit_str);
	ck_assert(!xstrcmp(out_str, "0x10000000000000002"));
	xfree(out_str);

	bit_free(bit_str);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(void)
{
	Suite *s = suite_create("bit_unfmt_hexmask test");
	TCase *tc_core = tcase_create("Testing bit_unfmt_hexmask");
	tcase_add_test(tc_core, null_test);
	tcase_add_test(tc_core, bounds_test);
	tcase_add_test(tc_core, good_test);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(suite());

	//srunner_set_fork_status(sr, CK_NOFORK);

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

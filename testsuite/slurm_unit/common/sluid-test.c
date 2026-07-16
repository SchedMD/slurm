#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/sluid.h"
#include "src/common/xmalloc.h"

#include <check.h>

START_TEST(test_sluid)
{
	sluid_t sluid = 0xbc064b4483ea1000, sluid2 = 0;
	char *str = NULL;

	str = sluid2str(sluid);
	ck_assert_msg((!strcmp(str, "sBR1JB8J1YM400")) , "sluid2str(sluid)");
	sluid2 = str2sluid(str);
	ck_assert_msg(sluid == sluid2, "str2sluid(sluid2str(sluid))");
	xfree(str);
}
END_TEST

int main(void)
{
	int number_failed;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("sluid-test", log_opts, 0, NULL);

	Suite *s = suite_create("sluid");
	TCase *tc_core = tcase_create("sluid");

	tcase_add_test(tc_core, test_sluid);

	suite_add_tcase(s, tc_core);

	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xbase64.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include <check.h>

START_TEST(test_xbase64)
{
	uint8_t payload[] = { 140, 0, 1, 0, 10, 0, 255, 0, 254, 2 };

	for (int i = 1; i <= 10; i++) {
		uint8_t *dec = NULL;
		char *enc = xbase64_encode(payload, i);
		int len = xbase64_decode(&dec, enc);
		ck_assert_msg((len == i), "length mismatch %d != %d", len, i);
		ck_assert_msg((!memcmp(dec, payload, i)),
			      "xbase64 encode then decode failed for %s %d",
			      enc, i);
		xfree(enc);
		xfree(dec);
	}
}

END_TEST

START_TEST(test_xbase64_rfc4648)
{
	struct {
		const char *plain;
		const char *encoded;
	} vectors[] = {
		{ "f", "Zg==" },
		{ "f""o", "Zm8=" },
		{ "foo", "Zm9v" },
		{ "foob", "Zm9vYg==" },
		{ "fooba", "Zm9vYmE=" },
		{ "foobar", "Zm9vYmFy" },
	};

	for (int i = 0; i < ARRAY_SIZE(vectors); i++) {
		size_t plen = strlen(vectors[i].plain);
		uint8_t *dec = NULL;
		char *enc;
		int declen;

		enc = xbase64_encode((const uint8_t *) vectors[i].plain, plen);
		ck_assert_msg(!xstrcmp(enc, vectors[i].encoded),
			      "encode of '%s' produced '%s', expected '%s'",
			      vectors[i].plain, enc, vectors[i].encoded);

		declen = xbase64_decode(&dec, vectors[i].encoded);
		ck_assert_msg(declen == (int) plen,
			      "decode of '%s' returned %d, expected %zu",
			      vectors[i].encoded, declen, plen);
		ck_assert_msg(!memcmp(dec, vectors[i].plain, declen),
			      "decode of '%s' did not match plaintext",
			      vectors[i].encoded);

		xfree(enc);
		xfree(dec);
	}
}

END_TEST

int main(void)
{
	int number_failed;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("xbase64-test", log_opts, 0, NULL);

	Suite *s = suite_create("xbase64");
	TCase *tc_core = tcase_create("xbase64");

	tcase_add_test(tc_core, test_xbase64);
	tcase_add_test(tc_core, test_xbase64_rfc4648);

	suite_add_tcase(s, tc_core);

	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

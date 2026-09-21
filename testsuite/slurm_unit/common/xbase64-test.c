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

static const char alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

START_TEST(test_xbase64_all_byte_values)
{
	uint8_t plain[256];
	uint8_t *dec = NULL;
	char *enc;
	int declen;

	for (int i = 0; i < ARRAY_SIZE(plain); i++)
		plain[i] = i;

	enc = xbase64_encode(plain, sizeof(plain));
	declen = xbase64_decode(&dec, enc);

	ck_assert_msg(declen == (int) sizeof(plain),
		      "decode returned %d, expected %zu", declen,
		      sizeof(plain));
	for (int i = 0; i < ARRAY_SIZE(plain); i++)
		ck_assert_msg(dec[i] == plain[i],
			      "byte %d decoded as %d, expected %d", i, dec[i],
			      plain[i]);

	xfree(enc);
	xfree(dec);
}

END_TEST

START_TEST(test_xbase64_decode_rejects_invalid)
{
	uint8_t sentinel;
	uint8_t *dec = &sentinel;

	/* Not a multiple of four. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zm9"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Empty input. */
	ck_assert_int_eq(xbase64_decode(&dec, ""), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Characters outside the alphabet. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zm9*"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Padding is only valid in the final quad. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zg==Zm9v"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Bytes with the high bit set. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zm9\xff"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;
	ck_assert_int_eq(xbase64_decode(&dec, "\x80m9v"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Invalid in the first two positions of the final quad. */
	ck_assert_int_eq(xbase64_decode(&dec, "=m9v"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;
	ck_assert_int_eq(xbase64_decode(&dec, "Z=9v"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Invalid in the third position when the fourth is padding. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zm*="), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Invalid in the third position when the fourth is not padding. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zm*v"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* Padding in the third position only. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zm=v"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;

	/* base64url characters must not decode as base64. */
	ck_assert_int_eq(xbase64_decode(&dec, "Zm9-"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;
	ck_assert_int_eq(xbase64_decode(&dec, "Zm9_"), -1);
	ck_assert(dec == NULL);
	dec = &sentinel;
}

END_TEST

START_TEST(test_xbase64_decode_byte_sweep)
{
	/* Byte 0 cannot reach the table: strlen() bounds the scan. */
	for (int value = 1; value <= UINT8_MAX; value++) {
		int valid = (memchr(alphabet, value, 64) != NULL);

		for (int pos = 0; pos < 4; pos++) {
			char enc[] = "AAAAAAAA";
			uint8_t sentinel;
			uint8_t *dec = &sentinel;
			int len;

			enc[pos] = (char) value;
			len = xbase64_decode(&dec, enc);

			if (valid) {
				ck_assert_msg(len == 6,
					      "byte %d at %d returned %d",
					      value, pos, len);
				xfree(dec);
			} else {
				ck_assert_msg(len == -1,
					      "byte %d at %d returned %d",
					      value, pos, len);
				ck_assert_msg(dec == NULL,
					      "byte %d at %d left dec set",
					      value, pos);
			}
		}
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
	tcase_add_test(tc_core, test_xbase64_all_byte_values);
	tcase_add_test(tc_core, test_xbase64_decode_rejects_invalid);
	tcase_add_test(tc_core, test_xbase64_decode_byte_sweep);

	suite_add_tcase(s, tc_core);

	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <src/common/log.h>
#include <src/common/pack.h>
#include <src/common/xmalloc.h>

#include <check.h>


START_TEST(test_pack)
{
	buf_t *buffer;
	uint16_t test16 = 1234, out16;
	uint32_t test32 = 5678, out32, byte_cnt;
	char testbytes[] = "TEST BYTES", *outbytes;
	char teststring[] = "TEST STRING",  *outstring = NULL;
	char *nullstr = NULL;
	char *data;
	int data_size;
	long double test_double = 1340664754944.2132312, test_double2;
	uint64_t test64;

	buffer = init_buf (0);
        pack16(test16, buffer);
        pack32(test32, buffer);
	pack64((uint64_t)test_double, buffer);

        packstr(testbytes, buffer);
        packstr(teststring, buffer);
	packstr(nullstr, buffer);

	packstr("literal", buffer);
	packstr("", buffer);

        data_size = get_buf_offset(buffer);
        printf("wrote %d bytes\n", data_size);

	/* Pull data off old buffer, destroy it, and create a new one */
	data = xfer_buf_data(buffer);
	buffer = create_buf(data, data_size);

        unpack16(&out16, buffer);
	info("out16 =%d", out16);
	info("test16=%d", test16);
	ck_assert_msg(out16 == test16, "un/pack16");

        unpack32(&out32, buffer);
	ck_assert_msg(out32 == test32, "un/pack32");

  	unpack64(&test64, buffer);
	test_double2 = (long double)test64;
	ck_assert_msg((uint64_t)test_double2 == (uint64_t)test_double, "un/pack double as a uint64");
	/* info("Original\t %Lf", test_double); */
	/* info("uint64\t %ld", test64); */
	/* info("converted LD\t %Lf", test_double2); */

	unpackmem_ptr(&outbytes, &byte_cnt, buffer);
	ck_assert_msg( ( strcmp(testbytes, outbytes) == 0 ) , "un/packstr_ptr");

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	ck_assert_msg(strcmp(teststring, outstring) == 0, "un/packstr_xmalloc");
	xfree(outstring);

	unpackstr_xmalloc(&nullstr, &byte_cnt, buffer);
	ck_assert_msg(nullstr == NULL, "un/packstr of null string.");

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	ck_assert_msg(strcmp("literal", outstring) == 0,
			"un/packstr of string literal");
	xfree(outstring);

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	ck_assert_msg(strcmp("", outstring) == 0, "un/packstr of string \"\" ");

	xfree(outstring);
	free_buf(buffer);
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

	tcase_add_test(tc_core, test_pack);

	suite_add_tcase(s, tc_core);

	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

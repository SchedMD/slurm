#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <src/common/pack.h>
#include <src/common/xmalloc.h>

#include <testsuite/dejagnu.h>

/* Test for failure: 
*/
#define TEST(_tst, _msg) do {			\
	if (_tst) 				\
		fail( _msg );       \
	else					\
		pass( _msg );       \
} while (0)

int main (int argc, char *argv[])
{
	Buf buffer;
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
	TEST(out16 != test16, "un/pack16");

        unpack32(&out32, buffer);
	TEST(out32 != test32, "un/pack32");

  	unpack64(&test64, buffer);
	test_double2 = (long double)test64;
	TEST((uint64_t)test_double2 != (uint64_t)test_double, "un/pack double as a uint64");
	/* info("Original\t %Lf", test_double); */
	/* info("uint64\t %ld", test64); */
	/* info("converted LD\t %Lf", test_double2); */

	unpackmem_ptr(&outbytes, &byte_cnt, buffer);
	TEST( ( strcmp(testbytes, outbytes) != 0 ) , "un/packstr_ptr");

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	TEST(strcmp(teststring, outstring) != 0, "un/packstr_xmalloc");
	xfree(outstring);

	unpackstr_xmalloc(&nullstr, &byte_cnt, buffer);
	TEST(nullstr != NULL, "un/packstr of null string.");

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	TEST(strcmp("literal", outstring) != 0, 
			"un/packstr of string literal");
	xfree(outstring);

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	TEST(strcmp("", outstring) != 0, "un/packstr of string \"\" ");

	xfree(outstring);

	free_buf(buffer);
	totals();
	return failed;

}

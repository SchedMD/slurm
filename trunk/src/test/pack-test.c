/* $Id$ */

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <src/common/pack.h>
#include <src/common/xmalloc.h>

/* Test for failure: 
 */
#define TEST(_tst, _msg) do {			\
	if (_tst) {				\
		printf("%s\n", _msg);   	\
		failed++;			\
	} else					\
		passed++;			\
} while (0)
	

int main (int argc, char *argv[])
{
	int passed = 0;
	int failed = 0;
	Buf buffer;
            
        uint16_t test16 = 1234, out16, byte_cnt;
        uint32_t test32 = 5678, out32;
        char testbytes[] = "TEST BYTES", *outbytes;
	char teststring[] = "TEST STRING",  *outstring = NULL;
	char *nullstr = NULL;
	char *data;
	int data_size;

	buffer = init_buf (0);
        pack16(test16, buffer);
        pack32(test32, buffer);

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
	TEST(out16 != test16, "un/pack16 failed");

        unpack32(&out32, buffer);
        TEST(out32 != test32, "un/pack32 failed");

        unpackstr_ptr(&outbytes, &byte_cnt, buffer);
        TEST(strcmp(testbytes, outbytes) != 0, "un/packstr_ptr failed");

        unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
        TEST(strcmp(teststring, outstring) != 0, "un/packstr_xmalloc failed");
	xfree(outstring);

	unpackstr_xmalloc(&nullstr, &byte_cnt, buffer);
	TEST(nullstr != NULL, "un/packstr of null string failed.");

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	TEST(strcmp("literal", outstring) != 0, 
	     "un/packstr of string literal failed");
	xfree(outstring);

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	TEST(strcmp("", outstring) != 0, "un/packstr of string \"\" failed");
	xfree(outstring);

	free_buf(buffer);
        printf("%d tests passed, %d failed.\n", passed, failed);

	return failed;
}


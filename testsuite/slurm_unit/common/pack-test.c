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
	char buffer[1024];
	void *bufp;
	int len_buf = 0;
	uint16_t test16 = 1234, out16;
	uint32_t test32 = 5678, out32, byte_cnt;
	char testbytes[] = "TEST BYTES", *outbytes;
	char teststring[] = "TEST STRING",  *outstring = NULL;
	char *nullstr = NULL;

	bufp = buffer;
	len_buf = sizeof(buffer);
	pack16(test16, &bufp, &len_buf);
	pack32(test32, &bufp, &len_buf);

	packstr(testbytes, &bufp, &len_buf);
	packstr(teststring, &bufp, &len_buf);
	packstr(nullstr, &bufp, &len_buf);

	packstr("literal", &bufp, &len_buf);
	packstr("", &bufp, &len_buf);
	printf("wrote %d bytes\n", len_buf);

	bufp = buffer;
	len_buf = sizeof(buffer);
	unpack16(&out16, &bufp, &len_buf);
	TEST(out16 != test16, "un/pack16");

	unpack32(&out32, &bufp, &len_buf);
	TEST(out32 != test32, "un/pack32");

	unpackstr_ptr(&outbytes, &byte_cnt, &bufp, &len_buf);
	TEST( ( strcmp(testbytes, outbytes) != 0 ) , "un/packstr_ptr");

	unpackstr_xmalloc(&outstring, &byte_cnt, &bufp, &len_buf);
	TEST(strcmp(teststring, outstring) != 0, "un/packstr_xmalloc");
	xfree(outstring);

	unpackstr_xmalloc(&nullstr, &byte_cnt, &bufp, &len_buf);
	TEST(nullstr != NULL, "un/packstr of null string.");

	unpackstr_xmalloc(&outstring, &byte_cnt, &bufp, &len_buf);
	TEST(strcmp("literal", outstring) != 0, 
			"un/packstr of string literal");
	xfree(outstring);

	unpackstr_xmalloc(&outstring, &byte_cnt, &bufp, &len_buf);
	TEST(strcmp("", outstring) != 0, "un/packstr of string \"\" ");
	xfree(outstring);

	totals();
	return failed;

}


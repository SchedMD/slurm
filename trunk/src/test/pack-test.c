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

int main (int argc, char *argv[])
{
	int passed = 0;
	int failed = 0;
            
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
        printf("wrote %d bytes\n", len_buf);

        bufp = buffer;
        len_buf = sizeof(buffer);
        unpack16(&out16, &bufp, &len_buf);

        if (out16 != test16) {
                printf("un/pack16 failed\n");
		failed++;
	} else
		passed++;

        unpack32(&out32, &bufp, &len_buf);

        if (out32 != test32) {
                printf("un/pack32 failed\n");
		failed++;
	} else
		passed++;

        unpackstr_ptr(&outbytes, &byte_cnt, &bufp, &len_buf);

        if (strcmp(testbytes, outbytes) != 0) {
                printf("un/packstr_ptr failed\n");
		failed++;
	} else
		passed++;

        unpackstr_xmalloc(&outstring, &byte_cnt, &bufp, &len_buf);

        if (strcmp(teststring, outstring) != 0) {
                printf("un/packstr_xmalloc failed\n");
		failed++;
	} else {
		passed++;
		xfree(outstring);
	}

	unpackstr_xmalloc(&nullstr, &byte_cnt, &bufp, &len_buf);

	if (nullstr != NULL) {
		printf("un/packstr of null string failed\n");
		failed++;
	} else 
		passed++;

        printf("%d tests passed, %d failed.\n", passed, failed);

	return failed;
}


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

int main (int argc, char *argv[])
{
	int passed = 0;
	int failed = 0;
            
        char buffer[1024];
        void *bufp;
        int len_buf = 0;
        uint16_t test16 = 1234, out16;
        uint32_t test32 = 5678, out32, byte_cnt;
        char testbytes[] = "TEST STRING", *outbytes;

        bufp = buffer;
        len_buf = sizeof(buffer);
        pack16(test16, &bufp, &len_buf);
        pack32(test32, &bufp, &len_buf);
        packstr(testbytes, (uint32_t)(strlen(testbytes)+1), &bufp, &len_buf);
        printf("wrote %d bytes\n", len_buf);

        bufp = buffer;
        len_buf = sizeof(buffer);
        unpack16(&out16, &bufp, &len_buf);
        unpack32(&out32, &bufp, &len_buf);
        unpackstr(&outbytes, &byte_cnt, &bufp, &len_buf);

        if (out16 != test16) {
                printf("un/pack16 failed\n");
		failed++;
	} else
		passed++;

        if (out32 != test32) {
                printf("un/pack32 failed\n");
	} else
		passed++;

        if (strcmp(testbytes, outbytes) != 0) {
                printf("un/packstr failed\n");
	} else
		passed++;

        printf("%d tests passed, %d failed.\n", passed, failed);

	return failed;
}


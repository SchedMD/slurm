/* 
 * $Id$
 *
 * Library routines for packing/unpacking integers.
 */

#include <stdint.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <assert.h>
#include "pack.h"
	
	
/*
 * Given a 32-bit integer in host byte order, convert to network byte order
 * and store at 'bufp'.  Advance bufp by 4 bytes, decrement lenp by 4 bytes.
 */
void
_pack32(uint32_t val, void **bufp, int *lenp)
{
	uint32_t nl = htonl(val);

	memcpy(*bufp, &nl, sizeof(nl));

	*bufp += sizeof(nl);
	*lenp -= sizeof(nl);
}

/*
 * Given 'bufp' pointing to a network byte order 32-bit integer,
 * store a host integer at 'valp'.  Advance bufp by 4 bytes, decrement
 * lenp by 4 bytes.
 */
void
_unpack32(uint32_t *valp, void **bufp, int *lenp)
{
	uint32_t nl;

	memcpy(&nl, *bufp, sizeof(nl));
	*valp = ntohl(nl);

	*bufp += sizeof(nl);
	*lenp -= sizeof(nl);
}
	
/*
 * Given a 16-bit integer in host byte order, convert to network byte order
 * and store at 'bufp'.  Advance bufp by 2 bytes, decrement lenp by 2 bytes.
 */
void
_pack16(uint16_t val, void **bufp, int *lenp)
{
	uint16_t ns = htons(val);

	memcpy(*bufp, &ns, sizeof(ns));

	*bufp += sizeof(ns);
	*lenp -= sizeof(ns);
}

/*
 * Given 'bufp' pointing to a network byte order 16-bit integer,
 * store a host integer at 'valp'.  Advance bufp by 2 bytes, decrement
 * lenp by 2 bytes.
 */
void
_unpack16(uint16_t *valp, void **bufp, int *lenp)
{
	uint16_t ns;

	memcpy(&ns, *bufp, sizeof(ns));
	*valp = ntohs(ns);

	*bufp += sizeof(ns);
	*lenp -= sizeof(ns);
}

/*
 * Given a pointer to memory (valp) and a size (size_val), convert 
 * size_val to network byte order and store at 'bufp' followed by 
 * the data at valp. Advance bufp and decrement lenp by 4 bytes 
 * (size of size_val) plus the value of size_val.
 */
void
_packstr(char *valp, uint32_t size_val, void **bufp, int *lenp)
{
	uint32_t nl = htonl(size_val);

	memcpy(*bufp, &nl, sizeof(nl));
	*bufp += sizeof(nl);
	*lenp -= sizeof(nl);

	memcpy(*bufp, valp, size_val);
	*bufp += size_val;
	*lenp -= size_val;

}

/*
 * Given 'bufp' pointing to a network byte order 32-bit integer
 * (size) and an arbitrary data string, return a pointer to the 
 * data string in 'valp'.  Also return the sizes of 'valp' in bytes. 
 * Advance bufp and decrement lenp by 4 bytes (size of memory 
 * size records) plus the actual buffer size.
 */
void
_unpackstr(char **valp, uint32_t *size_valp, void **bufp, int *lenp)
{
	uint32_t nl;

	memcpy(&nl, *bufp, sizeof(nl));
	*size_valp = ntohl(nl);
	*bufp += sizeof(nl);
	*lenp -= sizeof(nl);

	*valp = *bufp;
	*bufp += nl;
	*lenp -= nl;

}
	

#if DEBUG_MODULE

/* main is used here for testing purposes only */
main (int argc, char *argv[]) 
{
	char buffer[1024];
	void *bufp;
	int len_buf = 0;
	uint16_t test16 = 1234, out16;
	uint32_t test32 = 5678, out32, byte_cnt;
	char testbytes[] = "TEST STRING", *outbytes;

	bufp = buffer;
	len_buf = sizeof(buffer);
	pack16 (test16, &bufp, &len_buf);
	pack32 (test32, &bufp, &len_buf);
	packstr (testbytes, (uint32_t)(strlen(testbytes)+1), &bufp, &len_buf);
	printf ("wrote %d bytes\n", len_buf);

	bufp = buffer;
	len_buf = sizeof(buffer);
	unpack16 (&out16, &bufp, &len_buf);
	unpack32 (&out32, &bufp, &len_buf);
	unpackstr (&outbytes, &byte_cnt, &bufp, &len_buf);

	if (out16 != test16)
		printf("un/pack16 failed\n");
	if (out32 != test32)
		printf("un/pack32 failed\n");
	if (strcmp(testbytes, outbytes) != 0)
		printf("un/packstr failed\n");
	printf("test complete\n");
}

#endif

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
 * Given a 32-bit integer in host byte order, conver to network byte order
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
 * Given a 16-bit integer in host byte order, conver to network byte order
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

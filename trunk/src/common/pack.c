/* 
 * $Id$
 *
 * Library routines for packing/unpacking integers.
 */

#if HAVE_CONFIG_H
#  include <config.h> 
#endif
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include "pack.h"
#include "xmalloc.h"
	
	
/*
 * Given a 32-bit integer in host byte order, convert to network byte order
 * and store at 'bufp'.  Advance bufp by 4 bytes, decrement lenp by 4 bytes.
 */
void
_pack32(uint32_t val, void **bufp, int *lenp)
{
	uint32_t nl = htonl(val);

	memcpy(*bufp, &nl, sizeof(nl));

	(size_t)*bufp += sizeof(nl);
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

	(size_t)*bufp += sizeof(nl);
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

	(size_t)*bufp += sizeof(ns);
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

	(size_t)*bufp += sizeof(ns);
	*lenp -= sizeof(ns);
}

/*
 * Given a pointer to memory (valp) and a size (size_val), convert 
 * size_val to network byte order and store at 'bufp' followed by 
 * the data at valp. Advance bufp and decrement lenp by 4 bytes 
 * (size of size_val) plus the value of size_val.
 */
void
_packmem(char *valp, uint16_t size_val, void **bufp, int *lenp)
{
	uint16_t nl = htons(size_val);

	memcpy(*bufp, &nl, sizeof(nl));
	(size_t)*bufp += sizeof(nl);
	*lenp -= sizeof(nl);

	memcpy(*bufp, valp, size_val);
	(size_t)*bufp += size_val;
	*lenp -= size_val;

}


/*
 * Given 'bufp' pointing to a network byte order 32-bit integer
 * (size) and an arbitrary data string, return a pointer to the 
 * data string in 'valp'.  Also return the sizes of 'valp' in bytes. 
 * Advance bufp and decrement lenp by 4 bytes (size of memory 
 * size records) plus the actual buffer size.
 * NOTE: valp is set to point into the buffer bufp, a copy of 
 *	the data is not made
 */
void
_unpackmem_ptr(char **valp, uint16_t *size_valp, void **bufp, int *lenp)
{
	uint16_t nl;

	memcpy(&nl, *bufp, sizeof(nl));
	*size_valp = ntohs(nl);
	(size_t)*bufp += sizeof(nl);
	*lenp -= sizeof(nl);

	if (*size_valp > 0) {
		*valp = *bufp;
		(size_t)*bufp += *size_valp;
		*lenp -= *size_valp;
	}
	else
		*valp = NULL;

}
	

/*
 * Given 'bufp' pointing to a network byte order 32-bit integer
 * (size) and an arbitrary data string, return a pointer to the 
 * data string in 'valp'.  Also return the sizes of 'valp' in bytes. 
 * Advance bufp and decrement lenp by 4 bytes (size of memory 
 * size records) plus the actual buffer size.
 * NOTE: valp is set to point into a newly created buffer, 
 *	the caller is responsible for calling xfree on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
void
_unpackmem_xmalloc(char **valp, uint16_t *size_valp, void **bufp, int *lenp)
{
	uint16_t nl;

	memcpy(&nl, *bufp, sizeof(nl));
	*size_valp = ntohs(nl);
	(size_t)*bufp += sizeof(nl);
	*lenp -= sizeof(nl);

	if (*size_valp > 0) {
		*valp = xmalloc(*size_valp);
		memcpy (*valp, *bufp, *size_valp);
		(size_t)*bufp += *size_valp;
		*lenp -= *size_valp;
	}
	else
		*valp = NULL;

}
	/*
 * Given 'bufp' pointing to a network byte order 32-bit integer
 * (size) and an arbitrary data string, return a pointer to the 
 * data string in 'valp'.  Also return the sizes of 'valp' in bytes. 
 * Advance bufp and decrement lenp by 4 bytes (size of memory 
 * size records) plus the actual buffer size.
 * NOTE: valp is set to point into a newly created buffer, 
 *	the caller is responsible for calling xfree on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
void
_unpackmem_malloc(char **valp, uint16_t *size_valp, void **bufp, int *lenp)
{
	uint16_t nl;

	memcpy(&nl, *bufp, sizeof(nl));
	*size_valp = ntohs(nl);
	(size_t)*bufp += sizeof(nl);
	*lenp -= sizeof(nl);

	if (*size_valp > 0) {
		*valp = malloc(*size_valp);
		memcpy (*valp, *bufp, *size_valp);
		(size_t)*bufp += *size_valp;
		*lenp -= *size_valp;
	}
	else
		*valp = NULL;

}
	

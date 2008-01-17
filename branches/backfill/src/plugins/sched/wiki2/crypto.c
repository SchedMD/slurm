/*****************************************************************************\
 *  crypto.c - DES cryptographic routines.
 *****************************************************************************
 *  Produced by Cluster Resources, Inc., no rights reserved. 
\*****************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITERATION 4

/**************************************************************
 * des
 **************************************************************
 * DESCRIPTION
 * Compute a DES digest for a CRC according to a particular
 * key.
 *  
 * ARGUMENTS
 * lword (in/out) - The CRC to encode, which becomes the first
 *		lexical segment of the checksum.
 * irword (in/out ) - The key with which to encode the CRC,
 *		which becomes the second lexical segment of
 *		the checksum.
 *  
 * RETURNS
 * None.
 *
 * SOURCE
 * Cluster Resources, Inc., no rights reserved.
 **************************************************************/
static void des( uint32_t *lword, uint32_t *irword )
{
	int idx;
	uint32_t ia, ib, iswap, itmph, itmpl;

	static uint32_t c1[ MAX_ITERATION ] = {
		0xcba4e531,
		0x537158eb,
		0x145cdc3c,
		0x0d3fdeb2
	};
	static uint32_t c2[ MAX_ITERATION ] = {
		0x12be4590,
		0xab54ce58,
		0x6954c7a6,
		0x15a2ca46
	};

	itmph = 0;
	itmpl = 0;

	for ( idx = 0; idx < MAX_ITERATION; ++idx ) {
		iswap = *irword;
		ia = iswap ^ c1[ idx ];
		itmpl = ia & 0xffff;
		itmph = ia >> 16;
		ib = itmpl * itmpl + ~( itmph * itmph );
		ia = (ib >> 16) | ( (ib & 0xffff) << 16 );
		*irword = (*lword) ^ ( (ia ^c2[ idx ]) + (itmpl * itmph) );
		*lword = iswap;
	}
}

/**************************************************************
 * compute_crc
 **************************************************************
 * DESCRIPTION
 * Compute a cyclic redundancy check (CRC) character-wise.
 *  
 * ARGUMENTS
 * crc (in) - The CRC computed thus far.
 * onech (in) - The character to be added to the CRC.
 * 
 * RETURNS
 * The new CRC value.
 *
 * SOURCE
 * Cluster Resources, Inc., no rights reserved.
 **************************************************************/
static uint16_t compute_crc( uint16_t crc, uint8_t onech )
{
	int idx;
	uint32_t ans  = ( crc ^ onech << 8 );

	for ( idx = 0; idx < 8; ++idx ) {
		if ( ans & 0x8000 ) {
			ans <<= 1;
			ans = ans ^ 4129;
		} else {
			ans <<= 1;
		}
	}

	return ans;
}

/**************************************************************
 * checksum
 **************************************************************
 * DESCRIPTION
 * Compute a Wiki checksum for the current message contents
 * and return the result as a Wiki name-value pair.
 * 
 * ARGUMENTS
 * sum (out) - The string in which to store the resulting
 *		checksum.
 * key(in) - The seed value for the checksum.  This must be
 *		coordinated with the scheduler so that they
 *		both use the same value.  It is a string of
 *		ASCII decimal digits.
 *  
 * RETURNS
 * None.
 **************************************************************/
extern void checksum( char *sum, const char * key, const char * buf )
{
	uint32_t crc = 0;
	uint32_t lword, irword;
	int idx, buf_len = strlen(buf);
	uint32_t seed = (uint32_t) strtoul( key, NULL, 0 );

	for ( idx = 0; idx < buf_len; ++idx ) {
		crc = (uint32_t) compute_crc( crc, buf[idx] );
	}

	lword = crc;
	irword = seed;

	des( &lword, &irword );

	sprintf(sum, "CK=%08x%08x", lword, irword);
}

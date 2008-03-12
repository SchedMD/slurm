/*****************************************************************************\
 *  base64.c - encoding for communication with gold.
 *
 *  $Id: storage_filetxt.c 10893 2007-01-29 21:53:48Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "base64.h"
#include <string.h>

static char basis_64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* To tell if char is a valid base64 */
static int _is_base64(unsigned char c) {
	if((c >= '/' && c <= '9') 
	   || (c >= 'A' && c <= 'Z')
	   || (c >= 'a' && c <= 'z')
	   || (c == '+')) 
		return 1;
	return 0;
}

/*
 * encode_base64 - given a char * of in_len will return an encoded
 *                 version
 * IN in_str - pointer to string to be encoded
 * IN in_len - string length of in_str
 * RET pointer to encoded string or NULL on failure 
 * NOTE: allocates memory that should be xfreed with xfree.
 */
extern unsigned char *encode_base64(const unsigned char* in_str, 
				    unsigned int in_len)
{
	unsigned char *ret_str = NULL;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];
	int pos = 0;
	/* calculate the length of the result */
	int rlen = (in_len+2) / 3 * 4;	 /* encoded bytes */

	rlen++; /* for the eol */
	ret_str = xmalloc(sizeof(unsigned char) * rlen);
	
	debug4("encoding %s", in_str);

	while (in_len--) {
		char_array_3[i++] = *(in_str++);
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4)
				+ ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2)
				+ ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;
			
			for(i = 0; (i <4) ; i++)
				ret_str[pos++] = basis_64[char_array_4[i]];
			i = 0;
		}
	}
	
	if (i) {
		for(j = i; j < 3; j++)
			char_array_3[j] = '\0';
		
		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4)
			+ ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2)
			+ ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;
		
		for (j = 0; (j < i + 1); j++)
			ret_str[pos++] = basis_64[char_array_4[j]];
		
		while((i++ < 3))
			ret_str[pos++] = '=';
		
	}

	debug4("encoded %s", ret_str);
	
	return ret_str;
}

/*
 * decode_base64 - given a char * will return a decoded version
 *
 * IN in_str - pointer to string to be decoded
 * RET pointer to decoded string or NULL on failure
 * NOTE: allocates memory that should be xfreed with xfree.
 */
extern unsigned char *decode_base64(const unsigned char *in_str)
{
	int pos = 0;
	int in_len = strlen((char *)in_str);
	int i = 0;
	int j = 0;
	int in_pos = 0;
	unsigned char char_array_4[4], char_array_3[3];
	unsigned char *ret_str = NULL;

	int rlen = in_len * 3 / 4; /* always enough, but sometimes too
				    * much */
       	
	debug4("decoding %s", in_str);

	ret_str = xmalloc(sizeof(unsigned char) * rlen);
	memset(ret_str, 0, rlen);
	
	while (in_len-- && ( in_str[in_pos] != '=')
	       && _is_base64(in_str[in_pos])) {
		char_array_4[i++] = in_str[in_pos];
		in_pos++;
		if (i == 4) {
			for (i=0; i<4; i++) {
				int found = 0;
				while(basis_64[found] 
				      && basis_64[found] != char_array_4[i])
					found++;
				if(!basis_64[found]) 
					found = 0;
				char_array_4[i] = found;
			}
			char_array_3[0] = (char_array_4[0] << 2) 
				+ ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) 
				+ ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6)
				+ char_array_4[3];
			for (i = 0; i<3; i++)
				ret_str[pos++] = char_array_3[i];
			i = 0;
		}
	}

	if (i) {
		for (j=i; j<4; j++)
			char_array_4[j] = 0;

		for (j=0; j<4; j++) {
			int found = 0;
			while(basis_64[found] 
			      && basis_64[found] != char_array_4[j])
				found++;
			if(!basis_64[found]) 
				found = 0;
			
			char_array_4[j] = found;
		}

		char_array_3[0] = (char_array_4[0] << 2) 
			+ ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4)
			+ ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) 
			+ char_array_4[3];

		for (j = 0; (j < i - 1); j++)
			ret_str[pos++] = char_array_3[j];
	}

	debug4("decoded %s", ret_str);

	return ret_str;
}

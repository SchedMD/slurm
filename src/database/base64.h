/*****************************************************************************\
 *  base64.h - encoding for communication with gold.
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
#ifndef _HAVE_GOLD_BASE64_H
#define _HAVE_GOLD_BASE64_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include "src/common/xmalloc.h"

/*
 * encode_base64 - given a char * of in_len will return an encoded
 *                 version
 * IN in_str - pointer to string to be encoded
 * IN in_len - string length of in_str
 * RET pointer to encoded string 
 * NOTE: allocates memory that should be xfreed with xfree.
 */
extern unsigned char *encode_base64(const unsigned char *in_str,
				    unsigned int in_len);

/*
 * decode_base64 - given a char * will return a decoded version
 *
 * IN in_str - pointer to string to be decoded
 * RET pointer to decoded string
 * NOTE: allocates memory that should be xfreed with xfree.
 */
extern unsigned char *decode_base64(const unsigned char *in_str);

#endif

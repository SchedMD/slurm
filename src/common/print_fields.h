/*****************************************************************************\
 *  printfields.h - definitions for all printing functions.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
#ifndef __PRINT_FIELDS_H__
#define __PRINT_FIELDS_H__

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#  include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif
#include <time.h>
#include <unistd.h>

#include <slurm/slurm.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"

typedef struct {
	uint16_t len;  /* what is the width of the print */          
	char *name;  /* name to be printed in header */
	void (*print_routine) (); /* what is the function to print with  */
	uint16_t type; /* defined in the local function */
} print_field_t;

enum {
	PRINT_FIELDS_PARSABLE_NOT = 0,
	PRINT_FIELDS_PARSABLE_ENDING,
	PRINT_FIELDS_PARSABLE_NO_ENDING
};

extern int print_fields_parsable_print;
extern int print_fields_have_header;

extern void destroy_print_field(void *object);
extern void print_fields_header(List print_fields_list);
extern void print_fields_date(print_field_t *field, time_t value, int last);
extern void print_fields_str(print_field_t *field, char *value, int last);
extern void print_fields_int(print_field_t *field, int value, int last);
extern void print_fields_double(print_field_t *field, double value, int last);
extern void print_fields_long_double(
	print_field_t *field, long double value, int last);
extern void print_fields_uint32(
	print_field_t *field, uint32_t value, int last);
extern void print_fields_uint64(
	print_field_t *field, uint64_t value, int last);
extern void print_fields_time_from_mins(print_field_t *field,
					uint32_t value, int last);
extern void print_fields_time_from_secs(print_field_t *field, 
					uint32_t value, int last);
extern void print_fields_char_list(print_field_t *field, List value, int last);

#define print_fields_uint print_fields_uint32
#define print_fields_time print_fields_time_from_mins
#endif

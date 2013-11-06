/*****************************************************************************\
 * parse_spec.h - header for parser parse_spec.c
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _SLURM_PARSE_H_
#define	_SLURM_PARSE_H_

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else	/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

/*
 * slurm_parser - parse the supplied specification into keyword/value pairs
 *	only the keywords supplied will be searched for. the supplied
 *	specification is altered, overwriting the keyword and value pairs
 *	with spaces.
 * spec - pointer to the string of specifications, sets of three values
 *	(as many sets as required): keyword, type, value
 * IN keyword - string with the keyword to search for including equal sign
 *		(e.g. "name=")
 * IN type - char with value 'd'==int, 'f'==float, 's'==string, 'l'==long
 * IN/OUT value - pointer to storage location for value (char **) for type 's'
 * RET 0 if no error, otherwise errno code
 * NOTE: terminate with a keyword value of "END"
 * NOTE: values of type (char *) are xfreed if non-NULL. caller must xfree any
 *	returned value
 */
extern int slurm_parser (char *spec, ...) ;

/*
 * load_string  - parse a string for a keyword, value pair, and load the
 *	char value
 * IN/OUT destination - location into which result is stored, set to value,
 *	no change if value not found, if destination had previous value,
 *	that memory location is automatically freed
 * IN keyword - string to search for
 * IN/OUT in_line - string to search for keyword, the keyword and value
 *	(if present) are overwritten by spaces
 * RET 0 if no error, otherwise an error code
 * NOTE: destination must be free when no longer required
 * NOTE: if destination is non-NULL at function call time, it will be freed
 * NOTE: in_line is overwritten, do not use a constant
 */
extern int load_string (char **destination, char *keyword, char *in_line) ;

#endif

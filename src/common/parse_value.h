/*****************************************************************************\
 *  parse_value.h - helper functions to simplify typed values management in
 *                  Slurm parser (see parse_config.{h,c})
 *****************************************************************************
 *  Initially written by Francois Chevallier <chevallierfrancois@free.fr> @ BULL
 *  for slurm-2.6. Adapted by Matthieu Hautreux <matthieu.hautreux@cea.fr>, CEA,
 *  for slurm-14.11.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _PARSE_VALUE_H
#define _PARSE_VALUE_H

#include <stdint.h>

int s_p_handle_long(long* data, const char* key, const char* value);
int s_p_handle_uint16(uint16_t* data, const char* key, const char *value);
int s_p_handle_uint32(uint32_t* data, const char* key, const char* value);
int s_p_handle_uint64(uint64_t* data, const char* key, const char* value);
int s_p_handle_boolean(bool* data, const char* key, const char* value);
int s_p_handle_float(float* data, const char* key, const char* value);
int s_p_handle_double(double* data, const char* key, const char* value);
int s_p_handle_long_double(long double* data, const char* key,
			   const char* value);

#endif /* !_PARSE_VALUE_H */

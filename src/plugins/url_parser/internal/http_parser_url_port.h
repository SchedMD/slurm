/*****************************************************************************\
 http_parser_url_port.h - nodejs/http-parser url parser copy
 *****************************************************************************
 * Copyright Joyent, Inc. and other Node contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 \*****************************************************************************/

#ifndef _HTTP_PARSER_URL_PORT_H
#define _HTTP_PARSER_URL_PORT_H

#include "slurm/slurm.h"

enum url_fields {
	UF_SCHEMA = 0,
	UF_HOST = 1,
	UF_PORT = 2,
	UF_PATH = 3,
	UF_QUERY = 4,
	UF_FRAGMENT = 5,
	UF_USERINFO = 6,
	UF_MAX = 7,
};

struct parsed_url {
	uint16_t field_set; /* Bitmask of (1 << UF_*) values */
	uint16_t port; /* Converted UF_PORT string */

	struct {
		uint16_t off; /* Offset into buffer in which field starts */
		uint16_t len; /* Length of run in buffer */
	} field_data[UF_MAX];
};

extern int parse_url(const char *buf, size_t buflen, int is_connect,
		     struct parsed_url *u);

#endif

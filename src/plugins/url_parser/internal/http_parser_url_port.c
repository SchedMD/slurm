/*****************************************************************************\
 http_parser_url_port.c - nodejs/http-parser url parser copy
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

#include "http_parser_url_port.h"

#include "src/common/xassert.h"

#if HTTP_PARSER_STRICT
#define T(v) 0
#else
#define T(v) v
#endif

static const uint8_t normal_url_char[32] = {
/*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
        0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
/*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
        0    | T(2)   |   0    |   0    | T(16)  |   0    |   0    |   0,
/*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
        0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
/*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
        0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
/*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
        0    |   2    |   4    |   0    |   16   |   32   |   64   |  128,
/*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |   0,
/*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
/* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
        1    |   2    |   4    |   8    |   16   |   32   |   64   |   0,
};

#undef T

#define LOWER(c) (unsigned char) (c | 0x20)
#define IS_ALPHA(c) (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define IS_NUM(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c) (IS_ALPHA(c) || IS_NUM(c))

#define IS_ALPHA(c) (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define BIT_AT(a, i) \
	(!!((unsigned int) (a)[(unsigned int) (i) >> 3] & \
	    (1 << ((unsigned int) (i) & 7))))
#define IS_HEX(c) (IS_NUM(c) || (LOWER(c) >= 'a' && LOWER(c) <= 'f'))
#define IS_MARK(c) \
	((c) == '-' || (c) == '_' || (c) == '.' || (c) == '!' || (c) == '~' || \
	 (c) == '*' || (c) == '\'' || (c) == '(' || (c) == ')')
#define IS_USERINFO_CHAR(c) \
	(IS_ALPHANUM(c) || IS_MARK(c) || (c) == '%' || (c) == ';' || \
	 (c) == ':' || (c) == '&' || (c) == '=' || (c) == '+' || (c) == '$' || \
	 (c) == ',')

#if HTTP_PARSER_STRICT
#define IS_URL_CHAR(c) (BIT_AT(normal_url_char, (unsigned char) c))
#define IS_HOST_CHAR(c) (IS_ALPHANUM(c) || (c) == '.' || (c) == '-')
#else
#define IS_URL_CHAR(c) \
	(BIT_AT(normal_url_char, (unsigned char) c) || ((c) & 0x80))
#define IS_HOST_CHAR(c) \
	(IS_ALPHANUM(c) || (c) == '.' || (c) == '-' || (c) == '_')
#endif

enum http_parser_url_fields {
	UF_SCHEMA = 0,
	UF_HOST = 1,
	UF_PORT = 2,
	UF_PATH = 3,
	UF_QUERY = 4,
	UF_FRAGMENT = 5,
	UF_USERINFO = 6,
	UF_MAX = 7,
};

struct http_parser_url {
	uint16_t field_set; /* Bitmask of (1 << UF_*) values */
	uint16_t port; /* Converted UF_PORT string */

	struct {
		uint16_t off; /* Offset into buffer in which field starts */
		uint16_t len; /* Length of run in buffer */
	} field_data[UF_MAX];
};

enum state {
	S_DEAD = 1, /* important that this is > 0 */
	S_REQ_SPACES_BEFORE_URL,
	S_REQ_SCHEMA,
	S_REQ_SCHEMA_SLASH,
	S_REQ_SCHEMA_SLASH_SLASH,
	S_REQ_SERVER_START,
	S_REQ_SERVER,
	S_REQ_SERVER_WITH_AT,
	S_REQ_PATH,
	S_REQ_QUERY_STRING_START,
	S_REQ_QUERY_STRING,
	S_REQ_FRAGMENT_START,
	S_REQ_FRAGMENT,
};

enum http_host_state {
	S_HTTP_HOST_DEAD = 1,
	S_HTTP_USERINFO_START,
	S_HTTP_USERINFO,
	S_HTTP_HOST_START,
	S_HTTP_HOST_V6_START,
	S_HTTP_HOST,
	S_HTTP_HOST_V6,
	S_HTTP_HOST_V6_END,
	S_HTTP_HOST_V6_ZONE_START,
	S_HTTP_HOST_V6_ZONE,
	S_HTTP_HOST_PORT_START,
	S_HTTP_HOST_PORT,
};

static enum http_host_state http_parse_host_char(enum http_host_state s,
						 const char ch)
{
	switch (s) {
	case S_HTTP_USERINFO:
	case S_HTTP_USERINFO_START:
		if (ch == '@') {
			return S_HTTP_HOST_START;
		}

		if (IS_USERINFO_CHAR(ch)) {
			return S_HTTP_USERINFO;
		}
		break;

	case S_HTTP_HOST_START:
		if (ch == '[') {
			return S_HTTP_HOST_V6_START;
		}

		if (IS_HOST_CHAR(ch)) {
			return S_HTTP_HOST;
		}

		break;

	case S_HTTP_HOST:
		if (IS_HOST_CHAR(ch)) {
			return S_HTTP_HOST;
		}

	/* fall through */
	case S_HTTP_HOST_V6_END:
		if (ch == ':') {
			return S_HTTP_HOST_PORT_START;
		}

		break;

	case S_HTTP_HOST_V6:
		if (ch == ']') {
			return S_HTTP_HOST_V6_END;
		}

	/* fall through */
	case S_HTTP_HOST_V6_START:
		if (IS_HEX(ch) || ch == ':' || ch == '.') {
			return S_HTTP_HOST_V6;
		}

		if (s == S_HTTP_HOST_V6 && ch == '%') {
			return S_HTTP_HOST_V6_ZONE_START;
		}
		break;

	case S_HTTP_HOST_V6_ZONE:
		if (ch == ']') {
			return S_HTTP_HOST_V6_END;
		}

	/* fall through */
	case S_HTTP_HOST_V6_ZONE_START:
		/* RFC 6874 Zone ID consists of 1*( unreserved / pct-encoded) */
		if (IS_ALPHANUM(ch) || ch == '%' || ch == '.' || ch == '-' ||
		    ch == '_' || ch == '~') {
			return S_HTTP_HOST_V6_ZONE;
		}
		break;

	case S_HTTP_HOST_PORT:
	case S_HTTP_HOST_PORT_START:
		if (IS_NUM(ch)) {
			return S_HTTP_HOST_PORT;
		}

		break;

	default:
		break;
	}
	return S_HTTP_HOST_DEAD;
}

static int http_parse_host(const char *buf, struct http_parser_url *u,
			   int found_at)
{
	enum http_host_state s;

	const char *p;
	size_t buflen = u->field_data[UF_HOST].off + u->field_data[UF_HOST].len;

	xassert(u->field_set & (1 << UF_HOST));

	u->field_data[UF_HOST].len = 0;

	s = found_at ? S_HTTP_USERINFO_START : S_HTTP_HOST_START;

	for (p = buf + u->field_data[UF_HOST].off; p < buf + buflen; p++) {
		enum http_host_state new_s = http_parse_host_char(s, *p);

		if (new_s == S_HTTP_HOST_DEAD) {
			return 1;
		}

		switch (new_s) {
		case S_HTTP_HOST:
			if (s != S_HTTP_HOST) {
				u->field_data[UF_HOST].off =
					(uint16_t) (p - buf);
			}
			u->field_data[UF_HOST].len++;
			break;

		case S_HTTP_HOST_V6:
			if (s != S_HTTP_HOST_V6) {
				u->field_data[UF_HOST].off =
					(uint16_t) (p - buf);
			}
			u->field_data[UF_HOST].len++;
			break;

		case S_HTTP_HOST_V6_ZONE_START:
		case S_HTTP_HOST_V6_ZONE:
			u->field_data[UF_HOST].len++;
			break;

		case S_HTTP_HOST_PORT:
			if (s != S_HTTP_HOST_PORT) {
				u->field_data[UF_PORT].off =
					(uint16_t) (p - buf);
				u->field_data[UF_PORT].len = 0;
				u->field_set |= (1 << UF_PORT);
			}
			u->field_data[UF_PORT].len++;
			break;

		case S_HTTP_USERINFO:
			if (s != S_HTTP_USERINFO) {
				u->field_data[UF_USERINFO].off =
					(uint16_t) (p - buf);
				u->field_data[UF_USERINFO].len = 0;
				u->field_set |= (1 << UF_USERINFO);
			}
			u->field_data[UF_USERINFO].len++;
			break;

		default:
			break;
		}
		s = new_s;
	}

	/* Make sure we don't end somewhere unexpected */
	switch (s) {
	case S_HTTP_HOST_START:
	case S_HTTP_HOST_V6_START:
	case S_HTTP_HOST_V6:
	case S_HTTP_HOST_V6_ZONE_START:
	case S_HTTP_HOST_V6_ZONE:
	case S_HTTP_HOST_PORT_START:
	case S_HTTP_USERINFO:
	case S_HTTP_USERINFO_START:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum state parse_url_char(enum state s, const char ch)
{
	if (ch == ' ' || ch == '\r' || ch == '\n') {
		return S_DEAD;
	}

#if HTTP_PARSER_STRICT
	if (ch == '\t' || ch == '\f') {
		return S_DEAD;
	}
#endif

	switch (s) {
	case S_REQ_SPACES_BEFORE_URL:
		/*
		 * Proxied requests are followed by scheme of an absolute URI
		 * (alpha). All methods except CONNECT are followed by '/' or
		 * '*'.
		 */

		if (ch == '/' || ch == '*') {
			return S_REQ_PATH;
		}

		if (IS_ALPHA(ch)) {
			return S_REQ_SCHEMA;
		}

		break;

	case S_REQ_SCHEMA:
		if (IS_ALPHA(ch)) {
			return s;
		}

		if (ch == ':') {
			return S_REQ_SCHEMA_SLASH;
		}

		break;

	case S_REQ_SCHEMA_SLASH:
		if (ch == '/') {
			return S_REQ_SCHEMA_SLASH_SLASH;
		}

		break;

	case S_REQ_SCHEMA_SLASH_SLASH:
		if (ch == '/') {
			return S_REQ_SERVER_START;
		}

		break;

	case S_REQ_SERVER_WITH_AT:
		if (ch == '@') {
			return S_DEAD;
		}

	/* fall through */
	case S_REQ_SERVER_START:
	case S_REQ_SERVER:
		if (ch == '/') {
			return S_REQ_PATH;
		}

		if (ch == '?') {
			return S_REQ_QUERY_STRING_START;
		}

		if (ch == '@') {
			return S_REQ_SERVER_WITH_AT;
		}

		if (IS_USERINFO_CHAR(ch) || ch == '[' || ch == ']') {
			return S_REQ_SERVER;
		}

		break;

	case S_REQ_PATH:
		if (IS_URL_CHAR(ch)) {
			return s;
		}

		switch (ch) {
		case '?':
			return S_REQ_QUERY_STRING_START;

		case '#':
			return S_REQ_FRAGMENT_START;
		}

		break;

	case S_REQ_QUERY_STRING_START:
	case S_REQ_QUERY_STRING:
		if (IS_URL_CHAR(ch)) {
			return S_REQ_QUERY_STRING;
		}

		switch (ch) {
		case '?':
			/* allow extra '?' in query string */
			return S_REQ_QUERY_STRING;

		case '#':
			return S_REQ_FRAGMENT_START;
		}

		break;

	case S_REQ_FRAGMENT_START:
		if (IS_URL_CHAR(ch)) {
			return S_REQ_FRAGMENT;
		}

		switch (ch) {
		case '?':
			return S_REQ_FRAGMENT;

		case '#':
			return s;
		}

		break;

	case S_REQ_FRAGMENT:
		if (IS_URL_CHAR(ch)) {
			return s;
		}

		switch (ch) {
		case '?':
		case '#':
			return s;
		}

		break;

	default:
		break;
	}

	/* We should never fall out of the switch above unless there's an error */
	return S_DEAD;
}

int http_parser_parse_url(const char *buf, size_t buflen, int is_connect,
			  struct http_parser_url *u)
{
	enum state s;
	const char *p;
	enum http_parser_url_fields uf, old_uf;
	int found_at = 0;

	if (buflen == 0) {
		return 1;
	}

	u->port = u->field_set = 0;
	s = is_connect ? S_REQ_SERVER_START : S_REQ_SPACES_BEFORE_URL;
	old_uf = UF_MAX;

	for (p = buf; p < buf + buflen; p++) {
		s = parse_url_char(s, *p);

		/* Figure out the next field that we're operating on */
		switch (s) {
		case S_DEAD:
			return 1;

		/* Skip delimiters */
		case S_REQ_SCHEMA_SLASH:
		case S_REQ_SCHEMA_SLASH_SLASH:
		case S_REQ_SERVER_START:
		case S_REQ_QUERY_STRING_START:
		case S_REQ_FRAGMENT_START:
			continue;

		case S_REQ_SCHEMA:
			uf = UF_SCHEMA;
			break;

		case S_REQ_SERVER_WITH_AT:
			found_at = 1;

		/* fall through */
		case S_REQ_SERVER:
			uf = UF_HOST;
			break;

		case S_REQ_PATH:
			uf = UF_PATH;
			break;

		case S_REQ_QUERY_STRING:
			uf = UF_QUERY;
			break;

		case S_REQ_FRAGMENT:
			uf = UF_FRAGMENT;
			break;

		default:
			xassert(!"Unexpected state");
			return 1;
		}

		/* Nothing's changed; soldier on */
		if (uf == old_uf) {
			u->field_data[uf].len++;
			continue;
		}

		u->field_data[uf].off = (uint16_t) (p - buf);
		u->field_data[uf].len = 1;

		u->field_set |= (1 << uf);
		old_uf = uf;
	}

	/*
	 * host must be present if there is a schema
	 * parsing http:///toto will fail
	 */
	if ((u->field_set & (1 << UF_SCHEMA)) &&
	    (u->field_set & (1 << UF_HOST)) == 0) {
		return 1;
	}

	if (u->field_set & (1 << UF_HOST)) {
		if (http_parse_host(buf, u, found_at) != 0) {
			return 1;
		}
	}

	/* CONNECT requests can only contain "hostname:port" */
	if (is_connect && u->field_set != ((1 << UF_HOST) | (1 << UF_PORT))) {
		return 1;
	}

	if (u->field_set & (1 << UF_PORT)) {
		uint16_t off;
		uint16_t len;
		const char *p;
		const char *end;
		unsigned long v;

		off = u->field_data[UF_PORT].off;
		len = u->field_data[UF_PORT].len;
		end = buf + off + len;

		/*
		 * NOTE: The characters are already validated and are in the
		 * [0-9] range
		 */
		xassert((size_t) (off + len) <= buflen &&
			"Port number overflow");
		v = 0;
		for (p = buf + off; p < end; p++) {
			v *= 10;
			v += *p - '0';

			/* Ports have a max value of 2^16 */
			if (v > 0xffff) {
				return 1;
			}
		}

		u->port = (uint16_t) v;
	}

	return 0;
}

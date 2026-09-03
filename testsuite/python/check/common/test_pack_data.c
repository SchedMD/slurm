/*****************************************************************************\
 *  test_pack_data.c - unit test for pack_data() and unpack_data()
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <check.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm_errno.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/pack_data.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/xmalloc.h"

/*
 * How deep to probe for the nesting limit. The limit itself is private to
 * src/common/pack_data.c, so the tests find it rather than copy it. This only
 * has to sit above any limit that could be considered sane.
 */
#define DEPTH_PROBE_LIMIT 128

/* how many random buffers to throw at unpack_data() */
#define GARBAGE_ROUNDS 5000
#define GARBAGE_MAX_BYTES 64

/*
 * The largest entry count that fits a one byte count, so that both it and
 * the next one up get packed.
 */
#define COUNT_8_MAX 255

/*
 * Every byte pack_data() writes as a tag, so that half of the garbage starts
 * with one and drives the parser past the first byte. Kept here rather than
 * shared because data_pack_tag_t is private to src/common/pack_data.c.
 */
static const unsigned char pack_data_tags[] = {
	0x01, /* null */
	0x02, 0x03, 0x04, /* list, 8 16 and 32 bit counts */
	0x05, 0x06, 0x07, /* dictionary, 8 16 and 32 bit counts */
	0x08, 0x09, 0x0a, 0x0b, /* integer, 8 16 32 and 64 bits wide */
	0x0c, /* string, its own length behind it */
	0x0d, /* float, 64 bits wide */
	0x0e, /* bool, 8 bits wide */
};

/* every type a data_t can hold, in the order the permutations walk them */
static const data_type_t pack_data_types[] = {
	DATA_TYPE_NULL,   DATA_TYPE_LIST,  DATA_TYPE_DICT, DATA_TYPE_INT_64,
	DATA_TYPE_STRING, DATA_TYPE_FLOAT, DATA_TYPE_BOOL,
};

/*
 * Draw the next pseudo-random number.
 *
 * Not rand_r(), which only promises the same sequence within one C library.
 * The corpus below has to be the same bytes everywhere, or a failure found on
 * one platform cannot be repeated on another.
 *
 * IN/OUT state - generator state, which must not start at zero
 * RET the next value
 */
static uint32_t _xorshift32(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;

	return (*state = x);
}

/*
 * Pack data, unpack it into a new data_t, and compare the two.
 *
 * The comparison is made twice, because neither half catches what the other
 * does. data_check_match() converts before it compares, so it sees a lost
 * value but not a lost type: an integer and its decimal string match. Packing
 * the copy a second time catches that, since the tag differs, along with a
 * changed width or a reordered container. pack_data() always writes the same
 * bytes for the same data_t, so the two packings are comparable.
 *
 * IN data - data to round trip
 * RET true when the unpacked copy matches what went in
 */
static bool _round_trip(const data_t *data)
{
	buf_t *buffer = init_buf(0);
	buf_t *again = init_buf(0);
	data_t *out = data_new();
	bool match = false;

	if (!pack_data(data, SLURM_PROTOCOL_VERSION, buffer)) {
		const uint32_t len = get_buf_offset(buffer);

		set_buf_offset(buffer, 0);

		if (!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer) &&
		    !pack_data(out, SLURM_PROTOCOL_VERSION, again))
			match = (data_check_match(data, out, false) &&
				 (len == get_buf_offset(again)) &&
				 !memcmp(get_buf_data(buffer),
					 get_buf_data(again), len));
	}

	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);
	FREE_NULL_BUFFER(again);

	return match;
}

/*
 * Try to unpack bytes that were never packed by pack_data().
 * IN bytes - buffer contents to unpack
 * IN len - number of bytes
 * RET what unpack_data() returned
 */
static int _unpack_rc(const char *bytes, uint32_t len)
{
	char *data = xmalloc(len);
	buf_t *buffer;
	data_t *out = data_new();
	int rc = EINVAL;

	memcpy(data, bytes, len);
	buffer = create_buf(data, len);

	rc = unpack_data(out, SLURM_PROTOCOL_VERSION, buffer);

	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);

	return rc;
}

/*
 * Check the bytes pack_data() writes, and that they read back.
 *
 * The round trip tests cannot see this: pack_data() and unpack_data() move
 * together, so renumbering a tag or changing which width a value takes leaves
 * every one of them passing. These are the only assertions that pin the
 * format itself, which goes on the wire and cannot change.
 *
 * IN data - data to pack
 * IN bytes - exactly what pack_data() must write
 * IN len - number of bytes, which may hold a NUL
 * IN label - name for the failure message
 */
static void _golden(const data_t *data, const char *bytes, uint32_t len,
		    const char *label)
{
	buf_t *buffer = init_buf(0);
	char *copy = xmalloc(len);
	buf_t *wire;
	data_t *out = data_new();

	ck_assert_msg(!pack_data(data, SLURM_PROTOCOL_VERSION, buffer),
		      "%s: pack_data() failed", label);
	ck_assert_msg(get_buf_offset(buffer) == len,
		      "%s: packed %u bytes, expected %u", label,
		      get_buf_offset(buffer), len);
	ck_assert_msg(!memcmp(get_buf_data(buffer), bytes, len),
		      "%s: packed bytes differ from the expected ones", label);

	/* and the same bytes, handed back as if they came off the wire */
	memcpy(copy, bytes, len);
	wire = create_buf(copy, len);

	ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, wire),
		      "%s: unpack_data() failed", label);
	ck_assert_msg(data_check_match(data, out, false),
		      "%s: unpacked value differs", label);
	ck_assert_msg(remaining_buf(wire) == 0, "%s: %u bytes left unread",
		      label, remaining_buf(wire));

	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);
	FREE_NULL_BUFFER(wire);
}

/*
 * Check the tag and count a container is packed behind.
 *
 * A round trip cannot see this: pack_data() and unpack_data() agree on
 * whatever width is chosen, so an off-by-one at a count boundary still reads
 * back correctly. Only the bytes show which width was picked.
 *
 * IN data - container to pack
 * IN head - the tag and count it must start with
 * IN len - length of head
 * IN label - name for the failure message
 */
static void _check_head(const data_t *data, const char *head, uint32_t len,
			const char *label)
{
	buf_t *buffer = init_buf(0);

	ck_assert_msg(!pack_data(data, SLURM_PROTOCOL_VERSION, buffer),
		      "%s: pack_data() failed", label);
	ck_assert_msg(get_buf_offset(buffer) >= len, "%s: packed too little",
		      label);
	ck_assert_msg(!memcmp(get_buf_data(buffer), head, len),
		      "%s: wrong tag or count width", label);

	FREE_NULL_BUFFER(buffer);
}

/*
 * Give data the requested type.
 * IN/OUT data - data to populate
 * IN type - type to give it
 * RET the entry to descend into for a list or a dictionary, NULL for the
 *	types that hold no other data
 */
static data_t *_set_type(data_t *data, data_type_t type)
{
	switch (type) {
	case DATA_TYPE_NULL:
		data_set_null(data);
		return NULL;
	case DATA_TYPE_INT_64:
		data_set_int(data, -12345);
		return NULL;
	case DATA_TYPE_STRING:
		data_set_string(data, "value");
		return NULL;
	case DATA_TYPE_FLOAT:
		data_set_float(data, 1.5);
		return NULL;
	case DATA_TYPE_BOOL:
		data_set_bool(data, true);
		return NULL;
	case DATA_TYPE_LIST:
		data_set_list(data);
		return data_list_append(data);
	case DATA_TYPE_DICT:
		data_set_dict(data);
		return data_key_set(data, "key");
	default:
		ck_abort_msg("unexpected type %s", data_type_to_string(type));
		return NULL;
	}
}

/*
 * Name the shape being walked, for a failure message.
 * IN seq - types from the outermost inward
 * IN len - how many of them
 * RET the shape as text, valid until the next call
 */
static const char *_shape(const data_type_t *seq, int len)
{
	static char buf[128];
	size_t n = 0;

	buf[0] = '\0';

	for (int i = 0; (i < len) && (n < sizeof(buf)); i++)
		n += snprintf(buf + n, sizeof(buf) - n, "%s%s", i ? " > " : "",
			      data_type_to_string(seq[i]));

	return buf;
}

/*
 * Nest single entry lists, with a null at the bottom.
 * IN/OUT data - data to build the nesting into
 * IN levels - how many lists, which must be at least one
 */
static void _nest(data_t *data, int levels)
{
	data_t *at = data_set_list(data);

	for (int i = 1; i < levels; i++)
		at = data_set_list(data_list_append(at));

	data_set_null(data_list_append(at));
}

START_TEST(test_pack_data_scalars)
{
	data_t *data = data_new();

	data_set_null(data);
	ck_assert_msg(_round_trip(data), "null");

	data_set_bool(data, true);
	ck_assert_msg(_round_trip(data), "bool true");

	data_set_bool(data, false);
	ck_assert_msg(_round_trip(data), "bool false");

	data_set_string(data, "");
	ck_assert_msg(_round_trip(data), "empty string");

	data_set_string(data, "a string");
	ck_assert_msg(_round_trip(data), "string");

	data_set_string(data, "\xe2\x82\xac and \xc3\xa9");
	ck_assert_msg(_round_trip(data), "utf-8 string");

	/* a string of the bytes that are also tags */
	data_set_string(data, "\x02\x0c\x0e\x08");
	ck_assert_msg(_round_trip(data), "string of tag bytes");

	data_set_list(data);
	ck_assert_msg(_round_trip(data), "empty list");

	data_set_dict(data);
	ck_assert_msg(_round_trip(data), "empty dictionary");

	/* an empty key, and one long enough to need more than a byte */
	{
		char long_key[300];

		memset(long_key, 'k', sizeof(long_key) - 1);
		long_key[sizeof(long_key) - 1] = '\0';

		data_set_dict(data);
		data_set_int(data_key_set(data, ""), 1);
		ck_assert_msg(_round_trip(data), "empty key");

		data_set_dict(data);
		data_set_int(data_key_set(data, long_key), 1);
		ck_assert_msg(_round_trip(data), "long key");

		data_set_dict(data);
		data_set_int(data_key_set(data, "\xe2\x82\xac"), 1);
		ck_assert_msg(_round_trip(data), "utf-8 key");

		/* 127 characters is the last key the short form holds */
		long_key[127] = '\0';
		data_set_dict(data);
		data_set_int(data_key_set(data, long_key), 1);
		ck_assert_msg(_round_trip(data), "127 character key");
		_check_head(data, "\x05\x01\x7f", 3, "short form key length");

		long_key[127] = 'k';
		long_key[128] = '\0';
		data_set_dict(data);
		data_set_int(data_key_set(data, long_key), 1);
		ck_assert_msg(_round_trip(data), "128 character key");
		_check_head(data, "\x05\x01\x81\x80", 4,
			    "long form key length");
	}

	FREE_NULL_DATA(data);
}

END_TEST

START_TEST(test_pack_data_floats)
{
	/*
	 * Values that survive the trip. packdouble() stores the value scaled
	 * by FLOAT_MULT rather than the bit pattern, so this is not every
	 * double; see the limit pinned below.
	 */
	static const double values[] = {
		0.0,     -0.0,  1.5,    -1.5,    0.1,      -0.1,      1e-9,
		100.389, 1e300, -1e300, DBL_MIN, INFINITY, -INFINITY, NAN,
	};
	data_t *data = data_new();
	data_t *out = data_new();
	buf_t *buffer = init_buf(0);

	for (int i = 0; i < ARRAY_SIZE(values); i++) {
		data_set_float(data, values[i]);
		ck_assert_msg(_round_trip(data), "float %g", values[i]);
	}

	/*
	 * And the limit itself, so that it cannot quietly get worse. Anything
	 * above DBL_MAX/FLOAT_MULT overflows to infinity on the way in. This
	 * asserts what the format does today rather than what it should do.
	 */
	data_set_float(data, DBL_MAX);
	ck_assert_msg(!pack_data(data, SLURM_PROTOCOL_VERSION, buffer),
		      "pack_data() failed");
	set_buf_offset(buffer, 0);
	ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
		      "unpack_data() failed");
	ck_assert_msg(
		isinf(data_get_float(out)),
		"DBL_MAX no longer overflows; the FLOAT_MULT limit moved");

	FREE_NULL_DATA(data);
	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_pack_data_empty_strings)
{
	/*
	 * The length counts bytes and the terminator is not sent, so nothing
	 * empty puts a NUL on the wire: an empty string is its tag and a zero
	 * length, and an empty key is a zero length alone. Only the bytes
	 * show that, since a round trip is happy either way.
	 */
	data_t *data = data_new();

	/*
	 * The empty string alone is pinned by test_pack_data_golden. Start
	 * with the one byte case, so the framing is not a coincidence, and
	 * then take the shapes that test does not reach.
	 */
	_golden(data_set_string(data, "a"), "\x0c\x01\x61", 3,
		"one byte string");

	data_set_dict(data);
	data_set_int(data_key_set(data, ""), 1);
	_golden(data, "\x05\x01\x00\x08\x01", 5, "empty dictionary key");

	data_set_dict(data);
	data_set_string(data_key_set(data, ""), "");
	_golden(data, "\x05\x01\x00\x0c\x00", 5, "empty key and value");

	data_set_list(data);
	data_set_string(data_list_append(data), "");
	_golden(data, "\x02\x01\x0c\x00", 4, "list holding an empty string");

	/* an empty key does not collide with a one byte key */
	data_set_dict(data);
	data_set_int(data_key_set(data, ""), 1);
	data_set_int(data_key_set(data, "k"), 2);
	/* the empty key took an entry of its own, so the round trip has two */
	ck_assert_int_eq(data_get_dict_length(data), 2);
	ck_assert_msg(_round_trip(data), "empty key beside a named one");

	FREE_NULL_DATA(data);
}

END_TEST

START_TEST(test_pack_data_int_widths)
{
	/*
	 * An integer is packed at the narrowest width that holds it, so walk
	 * both sides of every width boundary. These are signed, so a value
	 * that narrows must sign extend back to what it was.
	 */
	static const int64_t values[] = {
		0,
		1,
		-1,
		INT8_MAX,
		INT8_MIN,
		((int64_t) INT8_MAX + 1),
		((int64_t) INT8_MIN - 1),
		INT16_MAX,
		INT16_MIN,
		((int64_t) INT16_MAX + 1),
		((int64_t) INT16_MIN - 1),
		INT32_MAX,
		INT32_MIN,
		((int64_t) INT32_MAX + 1),
		((int64_t) INT32_MIN - 1),
		INT64_MAX,
		INT64_MIN,
	};
	data_t *data = data_new();

	for (int i = 0; i < ARRAY_SIZE(values); i++) {
		data_set_int(data, values[i]);
		ck_assert_msg(_round_trip(data), "int %" PRId64, values[i]);
	}

	FREE_NULL_DATA(data);
}

END_TEST

START_TEST(test_pack_data_golden)
{
	data_t *data = data_new();

	_golden(data_set_null(data), "\x01", 1, "null");

	_golden(data_set_bool(data, true), "\x0e\x01", 2, "bool true");
	_golden(data_set_bool(data, false), "\x0e\x00", 2, "bool false");

	/* an integer takes the narrowest width that holds it */
	_golden(data_set_int(data, 0), "\x08\x00", 2, "int 0");
	_golden(data_set_int(data, -1), "\x08\xff", 2, "int -1");
	_golden(data_set_int(data, INT8_MAX), "\x08\x7f", 2, "int 127");
	_golden(data_set_int(data, INT8_MAX + 1), "\x09\x00\x80", 3, "int 128");
	_golden(data_set_int(data, INT16_MAX), "\x09\x7f\xff", 3, "int 32767");
	_golden(data_set_int(data, INT16_MAX + 1), "\x0a\x00\x00\x80\x00", 5,
		"int 32768");
	_golden(data_set_int(data, INT32_MAX), "\x0a\x7f\xff\xff\xff", 5,
		"int INT32_MAX");
	_golden(data_set_int(data, (int64_t) INT32_MAX + 1),
		"\x0b\x00\x00\x00\x00\x80\x00\x00\x00", 9, "int INT32_MAX + 1");

	/* a string carries its length in characters; the NUL is not sent */
	_golden(data_set_string(data, ""), "\x0c\x00", 2, "empty string");
	_golden(data_set_string(data, "hi"), "\x0c\x02\x68\x69", 4, "string");

	/*
	 * packdouble() stores the value scaled by FLOAT_MULT rather than the
	 * bit pattern, so these eight bytes are 1.5 * 1000000 as a double.
	 */
	_golden(data_set_float(data, 1.5),
		"\x0d\x41\x36\xe3\x60\x00\x00\x00\x00", 9, "float 1.5");

	_golden(data_set_list(data), "\x02\x00", 2, "empty list");
	_golden(data_set_dict(data), "\x05\x00", 2, "empty dictionary");

	data_set_list(data);
	data_set_int(data_list_append(data), 1);
	_golden(data, "\x02\x01\x08\x01", 4, "list of one");

	data_set_dict(data);
	data_set_bool(data_key_set(data, "k"), true);
	_golden(data, "\x05\x01\x01\x6b\x0e\x01", 6, "dictionary of one");

	FREE_NULL_DATA(data);
}

END_TEST

START_TEST(test_pack_data_counts)
{
	/*
	 * A container count is packed at the narrowest width that holds it,
	 * so cross the boundary between the 8 and 16 bit counts. The entries
	 * are distinct, so a reordering is caught as well as a miscount.
	 *
	 * The 16 to 32 bit boundary is not covered. It needs about 65535
	 * entries, and a container that large is quadratic to release:
	 * data.c's _release_data_list_node() walks the list to find each
	 * node's predecessor, so one such list costs about sixteen seconds in
	 * a developer build. test_unpack_data_wide_counts() reads a 32 bit
	 * count back instead, which covers the decoding but not the choice of
	 * width.
	 */
	static const struct {
		uint32_t count;
		const char *list_head;
		const char *dict_head;
		uint32_t head_len;
	} counts[] = {
		{ COUNT_8_MAX, "\x02\xff", "\x05\xff", 2 },
		{ (COUNT_8_MAX + 1), "\x03\x01\x00", "\x06\x01\x00", 3 },
	};

	data_t *data = data_new();

	for (int i = 0; i < ARRAY_SIZE(counts); i++) {
		data_set_list(data);

		for (uint32_t n = 0; n < counts[i].count; n++)
			data_set_int(data_list_append(data), n);

		ck_assert_msg(_round_trip(data), "list of %u", counts[i].count);
		_check_head(data, counts[i].list_head, counts[i].head_len,
			    "list count width");
	}

	for (int i = 0; i < ARRAY_SIZE(counts); i++) {
		data_set_dict(data);

		for (uint32_t n = 0; n < counts[i].count; n++) {
			char key[16];

			snprintf(key, sizeof(key), "%u", n);
			data_set_int(data_key_set(data, key), n);
		}

		ck_assert_msg(_round_trip(data), "dictionary of %u",
			      counts[i].count);
		_check_head(data, counts[i].dict_head, counts[i].head_len,
			    "dictionary count width");
	}

	FREE_NULL_DATA(data);
}

END_TEST

START_TEST(test_pack_data_string_widths)
{
	/*
	 * A string carries its own length in the same definite length form a
	 * dictionary key uses, so the short form ends at 127 rather than at
	 * 255. The length counts bytes and not the terminator, so 127
	 * bytes is the last string to fit it.
	 */
	char str[(COUNT_8_MAX * 2)];
	data_t *data = data_new();

	memset(str, 'x', sizeof(str));

	str[127] = '\0';
	data_set_string(data, str);
	ck_assert_msg(_round_trip(data), "string of 127");
	_check_head(data, "\x0c\x7f", 2, "short form string length");

	str[127] = 'x';
	str[128] = '\0';
	data_set_string(data, str);
	ck_assert_msg(_round_trip(data), "string of 128");
	_check_head(data, "\x0c\x81\x80", 3, "long form string length");

	/*
	 * The other two width transitions. Only the bytes show which form
	 * was written, and the three byte form is the one that splits the
	 * length by hand rather than handing it to a pack function.
	 */
	str[128] = 'x';
	str[255] = '\0';
	data_set_string(data, str);
	ck_assert_msg(_round_trip(data), "string of 255");
	_check_head(data, "\x0c\x81\xff", 3, "last one byte length");

	str[255] = 'x';
	str[256] = '\0';
	data_set_string(data, str);
	ck_assert_msg(_round_trip(data), "string of 256");
	_check_head(data, "\x0c\x82\x01\x00", 4, "first two byte length");

	{
		char *big = xmalloc(0x10001);

		memset(big, 'x', 0xffff);
		data_set_string(data, big);
		ck_assert_msg(_round_trip(data), "string of 65535");
		_check_head(data, "\x0c\x82\xff\xff", 4,
			    "last two byte length");

		memset(big, 'x', 0x10000);
		data_set_string(data, big);
		ck_assert_msg(_round_trip(data), "string of 65536");
		_check_head(data, "\x0c\x83\x01\x00\x00", 5,
			    "first three byte length");
		xfree(big);
	}

	/* a zero length is an empty string, and stays a string */
	{
		char *copy = xmalloc(2);
		buf_t *buffer;
		data_t *out = data_new();

		memcpy(copy, "\x0c\x00", 2);
		buffer = create_buf(copy, 2);

		ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
			      "zero length string was rejected");
		ck_assert_msg(data_get_type(out) == DATA_TYPE_STRING,
			      "zero length string did not stay a string");
		ck_assert_str_eq(data_get_string(out), "");

		FREE_NULL_DATA(out);
		FREE_NULL_BUFFER(buffer);
	}

	FREE_NULL_DATA(data);
}

END_TEST

START_TEST(test_unpack_data_key_lengths)
{
	/*
	 * A key length is written the way X.690 writes a definite length: a
	 * byte under 128 is the length, and above it the low bits count the
	 * bytes that follow. Only the shortest form of a length is valid, so
	 * one length has one spelling on the wire and a message that is
	 * unpacked and packed again comes back the same.
	 */
	ck_assert_int_eq(_unpack_rc("\x05\x01\x81\x01\x6b\x0e\x01", 7),
			 SLURM_ERROR);
	ck_assert_int_eq(_unpack_rc("\x05\x01\x82\x00\x01\x6b\x0e\x01", 8),
			 SLURM_ERROR);

	/* the indefinite form, which is not a length at all */
	ck_assert_int_eq(_unpack_rc("\x05\x01\x80\x6b\x0e\x01", 6),
			 SLURM_ERROR);

	/*
	 * Wider than four bytes, which would not fit a uint32_t. The length
	 * below is otherwise well formed and the key that follows it is
	 * complete, so nothing but the width check can reject this. Reading
	 * five bytes would also shift a uint32_t by 32, which is undefined.
	 */
	{
		char wide[(2 + 6 + 128 + 2)];
		int n = 0;

		wide[n++] = 0x05; /* DATA_TAG_DICT_8 */
		wide[n++] = 0x01; /* one entry */
		wide[n++] = 0x85; /* five length bytes follow */
		wide[n++] = 0x00;
		wide[n++] = 0x00;
		wide[n++] = 0x00;
		wide[n++] = 0x00;
		wide[n++] = 0x80; /* a key length of 128 */

		for (int i = 0; i < 128; i++)
			wide[n++] = 'k';

		wide[n++] = 0x0e; /* DATA_TAG_BOOL_8 */
		wide[n++] = 0x01;

		ck_assert_int_eq(_unpack_rc(wide, n), SLURM_ERROR);
	}

	/*
	 * The terminator is not sent, so a NUL among the bytes was not put
	 * there by pack_data(). Left in, it would truncate the string, and two
	 * dictionary keys differing only past a NUL would collide on the
	 * shorter of the two.
	 */
	ck_assert_int_eq(_unpack_rc("\x0c\x05\x41\x42\x00\x43\x44", 7),
			 SLURM_ERROR);
	ck_assert_int_eq(
		_unpack_rc("\x05\x02\x01\x61\x08\x01\x03\x61\x00\x62\x08\x02",
			   12),
		SLURM_ERROR);

	/*
	 * The same key twice. data_key_set() would hand back the first
	 * entry, so the second value would replace it and the dictionary
	 * would come back shorter than the wire said.
	 */
	ck_assert_int_eq(_unpack_rc("\x05\x02\x01\x6b\x08\x01\x01\x6b\x08\x02",
				    10),
			 SLURM_ERROR);

	/* the sentinels, which are reserved rather than sizes */
	ck_assert_int_eq(_unpack_rc("\x05\x01\x84\xff\xff\xff\xff", 7),
			 SLURM_ERROR);
	ck_assert_int_eq(_unpack_rc("\x05\x01\x84\xff\xff\xff\xfe", 7),
			 SLURM_ERROR);
}

END_TEST

START_TEST(test_unpack_data_wide_counts)
{
	/*
	 * A count wider than the entries need is still a count. pack_data()
	 * only writes the narrowest one, so these come from a peer that chose
	 * otherwise, and they are the only coverage of reading a 32 bit
	 * count.
	 */
	data_t *out = data_new();
	data_t *entry = NULL;

	/* a 32 bit list count, holding two entries */
	{
		const char bytes[] = "\x04\x00\x00\x00\x02\x08\x01\x08\x02";
		const uint32_t len = sizeof(bytes) - 1;
		char *copy = xmalloc(len);
		buf_t *buffer;

		memcpy(copy, bytes, len);
		buffer = create_buf(copy, len);

		ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
			      "32 bit list count was rejected");
		ck_assert_msg(data_get_type(out) == DATA_TYPE_LIST,
			      "not a list");
		ck_assert_int_eq(data_get_list_length(out), 2);
		ck_assert_msg(remaining_buf(buffer) == 0, "bytes left unread");

		FREE_NULL_BUFFER(buffer);
	}

	/* a 32 bit dictionary count, holding one entry */
	{
		const char bytes[] = "\x07\x00\x00\x00\x01\x01\x6b\x0e\x01";
		const uint32_t len = sizeof(bytes) - 1;
		char *copy = xmalloc(len);
		buf_t *buffer;

		memcpy(copy, bytes, len);
		buffer = create_buf(copy, len);

		ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
			      "32 bit dictionary count was rejected");
		ck_assert_msg(data_get_type(out) == DATA_TYPE_DICT,
			      "not a dictionary");
		ck_assert_msg((entry = data_key_get(out, "k")),
			      "key is missing");
		ck_assert_msg(data_get_bool(entry), "value is wrong");
		ck_assert_msg(remaining_buf(buffer) == 0, "bytes left unread");

		FREE_NULL_BUFFER(buffer);
	}

	FREE_NULL_DATA(out);
}

END_TEST

/*
 * Round trip every shape that fits within the given depth.
 *
 * A list or a dictionary descends one level further; any other type ends the
 * shape there. Each shape is packed as it is reached, so the shorter ones are
 * covered on the way down rather than by repeating the walk.
 *
 * IN/OUT root - outermost data, packed once per shape
 * IN/OUT at - entry to give the next type to
 * IN/OUT seq - types so far, for the failure message
 * IN level - how deep at sits
 * IN max - deepest level to build
 */
static void _walk_shapes(const data_t *root, data_t *at, data_type_t *seq,
			 int level, int max)
{
	if (level >= max)
		return;

	for (int i = 0; i < ARRAY_SIZE(pack_data_types); i++) {
		data_t *next = NULL;

		seq[level] = pack_data_types[i];
		next = _set_type(at, seq[level]);

		ck_assert_msg(_round_trip(root), "%s",
			      _shape(seq, (level + 1)));

		if (next)
			_walk_shapes(root, next, seq, (level + 1), max);
	}
}

START_TEST(test_pack_data_depth)
{
	data_t *data = data_new();
	data_type_t seq[4] = { 0 };

	_walk_shapes(data, data, seq, 0, ARRAY_SIZE(seq));

	FREE_NULL_DATA(data);
}

END_TEST

/*
 * Find the deepest nesting pack_data() accepts.
 *
 * The limit is private to pack_data.c. Probing for it keeps the assertions
 * about behaviour rather than about a constant the test cannot see, and means
 * a copy of it cannot drift.
 *
 * RET the deepest level that packed
 */
static int _find_max_depth(void)
{
	data_t *data = data_new();
	int deepest = 0;

	for (int levels = 1; levels <= DEPTH_PROBE_LIMIT; levels++) {
		buf_t *buffer = init_buf(0);
		int rc = EINVAL;

		_nest(data, levels);
		rc = pack_data(data, SLURM_PROTOCOL_VERSION, buffer);
		FREE_NULL_BUFFER(buffer);

		if (rc)
			break;

		deepest = levels;
	}

	FREE_NULL_DATA(data);

	return deepest;
}

START_TEST(test_pack_data_max_depth)
{
	const int deepest = _find_max_depth();
	data_t *data = data_new();
	buf_t *buffer = init_buf(0);

	/*
	 * Too shallow and a legitimately nested payload stops fitting; too
	 * deep and the recursion is no longer bounded well short of the
	 * stack. The band is what notices the limit being moved.
	 */
	ck_assert_msg((deepest >= 8) && (deepest <= 64),
		      "pack_data() accepts %d levels, outside the usable band",
		      deepest);

	/* whatever it accepts has to survive the trip back */
	_nest(data, deepest);
	ck_assert_msg(_round_trip(data), "nesting at the limit was refused");

	/*
	 * One deeper is refused while packing, rather than written into a
	 * buffer that unpack_data() would always reject.
	 */
	_nest(data, (deepest + 1));
	ck_assert_int_eq(pack_data(data, SLURM_PROTOCOL_VERSION, buffer),
			 SLURM_ERROR);

	FREE_NULL_DATA(data);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_pack_data_null_args)
{
	buf_t *buffer = init_buf(0);
	data_t *data = data_set_int(data_new(), 1);

	ck_assert_int_eq(pack_data(NULL, SLURM_PROTOCOL_VERSION, buffer),
			 EINVAL);
	ck_assert_int_eq(pack_data(data, SLURM_PROTOCOL_VERSION, NULL), EINVAL);
	ck_assert_int_eq(unpack_data(NULL, SLURM_PROTOCOL_VERSION, buffer),
			 EINVAL);
	ck_assert_int_eq(unpack_data(data, SLURM_PROTOCOL_VERSION, NULL),
			 EINVAL);

	FREE_NULL_DATA(data);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_pack_data_adjacent)
{
	/*
	 * A data_t has to sit in a buffer beside other fields, which is the
	 * reason for packing one at all. Nothing else here would catch an
	 * offset that moved too far or not far enough.
	 */
	buf_t *buffer = init_buf(0);
	data_t *first = data_set_string(data_new(), "first");
	data_t *second = data_set_int(data_new(), -12345);
	data_t *out = data_new();
	uint32_t head = 0, tail = 0, packed = 0;

	pack32(0xdeadbeef, buffer);
	ck_assert_msg(!pack_data(first, SLURM_PROTOCOL_VERSION, buffer),
		      "first pack_data() failed");
	ck_assert_msg(!pack_data(second, SLURM_PROTOCOL_VERSION, buffer),
		      "second pack_data() failed");
	pack32(0xcafebabe, buffer);

	/*
	 * What was written, which is not size_buf(): init_buf() allocates
	 * ahead, so the buffer is far larger than the bytes put in it.
	 */
	packed = get_buf_offset(buffer);

	set_buf_offset(buffer, 0);

	ck_assert_msg(!unpack32(&head, buffer), "leading sentinel");
	ck_assert_int_eq(head, 0xdeadbeef);

	ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
		      "first unpack_data() failed");
	ck_assert_msg(data_check_match(first, out, false), "first value");

	ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
		      "second unpack_data() failed");
	ck_assert_msg(data_check_match(second, out, false), "second value");

	ck_assert_msg(!unpack32(&tail, buffer), "trailing sentinel");
	ck_assert_int_eq(tail, 0xcafebabe);

	ck_assert_msg(get_buf_offset(buffer) == packed,
		      "read %u of the %u bytes written", get_buf_offset(buffer),
		      packed);

	FREE_NULL_DATA(first);
	FREE_NULL_DATA(second);
	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_unpack_data_reuse)
{
	/*
	 * unpack_data() takes an existing data_t, so a caller reading several
	 * values out of one buffer will hand it the same one each time. What
	 * it held before must go.
	 */
	buf_t *buffer = init_buf(0);
	data_t *dict = data_set_dict(data_new());
	data_t *scalar = data_set_int(data_new(), 7);
	data_t *out = data_new();

	data_set_int(data_key_set(dict, "one"), 1);
	data_set_int(data_key_set(dict, "two"), 2);

	ck_assert_msg(!pack_data(dict, SLURM_PROTOCOL_VERSION, buffer),
		      "pack_data() failed");
	ck_assert_msg(!pack_data(scalar, SLURM_PROTOCOL_VERSION, buffer),
		      "pack_data() failed");

	set_buf_offset(buffer, 0);

	ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
		      "unpack_data() failed");
	ck_assert_msg(data_check_match(dict, out, false), "dictionary");

	/* the same data_t again, this time taking a scalar */
	ck_assert_msg(!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer),
		      "unpack_data() failed");
	ck_assert_msg(data_get_type(out) == DATA_TYPE_INT_64,
		      "reused data_t kept its old type");
	ck_assert_int_eq(data_get_int(out), 7);

	FREE_NULL_DATA(dict);
	FREE_NULL_DATA(scalar);
	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_unpack_data_malformed)
{
	/* 0x00 and 0xff are deliberately not tags */
	ck_assert_int_eq(_unpack_rc("\x00", 1), SLURM_ERROR);
	ck_assert_int_eq(_unpack_rc("\xff", 1), SLURM_ERROR);
	ck_assert_int_eq(_unpack_rc("\x0f", 1), SLURM_ERROR);

	/* a tag with the value it promised missing */
	ck_assert_int_eq(_unpack_rc("\x0b", 1), SLURM_ERROR);
	ck_assert_int_eq(_unpack_rc("\x0c\x05\x68", 3), SLURM_ERROR);

	/* containers claiming more entries than the buffer can hold */
	ck_assert_int_eq(_unpack_rc("\x04\xff\xff\xff\xff", 5), SLURM_ERROR);
	ck_assert_int_eq(_unpack_rc("\x02\xc8", 2), SLURM_ERROR);
	ck_assert_int_eq(_unpack_rc("\x05\x01", 2), SLURM_ERROR);

	/* a dictionary entry with a key but no value */
	ck_assert_int_eq(_unpack_rc("\x05\x01\x01\x6b", 4), SLURM_ERROR);
}

END_TEST

START_TEST(test_unpack_data_too_many)
{
	/*
	 * Depth bounds how deep a message goes, not how wide. Every entry
	 * costs a data_t and a list node that data.c takes with xmalloc() and
	 * cannot refuse, and one wire byte asks for another, so a message
	 * that is small and entirely well formed can still ask for more than
	 * the heap will give. The counts below are honest, which is the
	 * point: nothing but the entry budget can refuse the larger one.
	 *
	 * The budget is private to pack_data.c, so bracket it rather than
	 * name it: a thousand entries is a payload anything would take, and
	 * a million is one nothing should.
	 */
	const uint32_t few = 1000, many = 1000000;
	char *wire = xmalloc(5 + many);

	wire[0] = 0x04; /* DATA_TAG_LIST_32 */
	memset(&wire[5], 0x01, many); /* DATA_TAG_NULL each */

	wire[1] = (few >> 24);
	wire[2] = (few >> 16);
	wire[3] = (few >> 8);
	wire[4] = few;
	ck_assert_int_eq(_unpack_rc(wire, (5 + few)), SLURM_SUCCESS);

	wire[1] = (many >> 24);
	wire[2] = (many >> 16);
	wire[3] = (many >> 8);
	wire[4] = many;
	ck_assert_int_eq(_unpack_rc(wire, (5 + many)), SLURM_ERROR);

	xfree(wire);
}

END_TEST

START_TEST(test_unpack_data_too_deep)
{
	/*
	 * Single entry lists one level past what unpack_data() accepts.
	 * pack_data() will not write this, so the bytes are built here.
	 */
	const int levels = (_find_max_depth() + 1);
	char *nested = xmalloc((levels * 2) + 1);
	int i = 0;

	for (int n = 0; n < levels; n++) {
		nested[i++] = 0x02; /* DATA_TAG_LIST_8 */
		nested[i++] = 0x01; /* one entry */
	}
	nested[i++] = 0x01; /* DATA_TAG_NULL */

	ck_assert_int_eq(_unpack_rc(nested, i), SLURM_ERROR);

	xfree(nested);
}

END_TEST

START_TEST(test_safe_unpack_data)
{
	buf_t *buffer = init_buf(0);
	data_t *in = data_set_string(data_new(), "through the macro");
	data_t *out = data_new();

	ck_assert_msg(!pack_data(in, SLURM_PROTOCOL_VERSION, buffer),
		      "pack_data() failed");
	set_buf_offset(buffer, 0);

	safe_unpack_data(out, SLURM_PROTOCOL_VERSION, buffer);

	ck_assert_msg(data_check_match(in, out, false),
		      "safe_unpack_data() did not give back what went in");

	FREE_NULL_DATA(in);
	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);
	return;

unpack_error:
	FREE_NULL_DATA(in);
	FREE_NULL_DATA(out);
	FREE_NULL_BUFFER(buffer);
	ck_abort_msg("safe_unpack_data() took the unpack_error path");
}

END_TEST

START_TEST(test_unpack_data_garbage)
{
	/* a fixed seed, so that anything this finds can be repeated */
	uint32_t seed = 0x5a5a5a5a;

	for (int i = 0; i < GARBAGE_ROUNDS; i++) {
		char bytes[GARBAGE_MAX_BYTES];
		uint32_t len = 1 + (_xorshift32(&seed) % sizeof(bytes));
		char *copy = xmalloc(len);
		buf_t *buffer;
		data_t *out = data_new();

		for (uint32_t b = 0; b < len; b++)
			bytes[b] = _xorshift32(&seed);

		/* half of them open with a tag that unpack_data() knows */
		if (i % 2)
			bytes[0] = pack_data_tags[_xorshift32(&seed) %
						  ARRAY_SIZE(pack_data_tags)];

		memcpy(copy, bytes, len);
		buffer = create_buf(copy, len);

		/*
		 * Random bytes can be a valid packing by chance, so the return
		 * is not asserted on. An accepted buffer has to repack. A
		 * rejected one is only required to return at all: what the
		 * round buys is that unpack_data() did not crash, abort,
		 * corrupt the heap or hang, which libcheck's fork mode and
		 * timeout report on their own.
		 *
		 * Staying inside the buffer is deliberately not asserted here.
		 * processed <= size is an invariant of the buf_t API, so the
		 * assertion could not fail, and an overread that never
		 * advances processed would satisfy it anyway. Run the corpus
		 * under a sanitizer to check bounds.
		 */
		if (!unpack_data(out, SLURM_PROTOCOL_VERSION, buffer))
			ck_assert_msg(_round_trip(out),
				      "round %d cannot be repacked", i);

		FREE_NULL_DATA(out);
		FREE_NULL_BUFFER(buffer);
	}
}

END_TEST

/*
 * Keep the rejection logging out of the test output.
 *
 * The error path tests reject thousands of buffers, and every one of them is
 * logged. Done as a fixture so that the level is put back even when an
 * assertion ends the test early, which matters under CK_FORK=no.
 */
static void _quiet_setup(void)
{
	log_options_t quiet = LOG_OPTS_INITIALIZER;

	quiet.stderr_level = LOG_LEVEL_FATAL;
	log_alter(quiet, 0, NULL);
}

static void _quiet_teardown(void)
{
	log_options_t loud = LOG_OPTS_INITIALIZER;

	loud.stderr_level = LOG_LEVEL_DEBUG5;
	log_alter(loud, 0, NULL);
}

extern int main(int argc, char **argv)
{
	int failures;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	TCase *tcase = tcase_create("pack_data");
	TCase *tcase_errors = tcase_create("pack_data_errors");
	Suite *suite = suite_create("pack_data");
	SRunner *sr = NULL;

	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("test_pack_data", log_opts, 0, NULL);

	tcase_add_test(tcase, test_pack_data_scalars);
	tcase_add_test(tcase, test_pack_data_floats);
	tcase_add_test(tcase, test_pack_data_empty_strings);
	tcase_add_test(tcase, test_pack_data_int_widths);
	tcase_add_test(tcase, test_pack_data_golden);
	tcase_add_test(tcase, test_pack_data_counts);
	tcase_add_test(tcase, test_pack_data_string_widths);
	tcase_add_test(tcase, test_unpack_data_wide_counts);
	tcase_add_test(tcase, test_pack_data_depth);
	tcase_add_test(tcase, test_pack_data_max_depth);
	tcase_add_test(tcase, test_pack_data_null_args);
	tcase_add_test(tcase, test_pack_data_adjacent);
	tcase_add_test(tcase, test_unpack_data_reuse);
	tcase_add_test(tcase, test_safe_unpack_data);

	/*
	 * The permutation walk and the count tests are not quick, but the
	 * harness kills the whole binary at atf.default_command_timeout,
	 * which is sixty seconds. Stay below it, so an overrun is reported
	 * as a named test failure instead of a kill with no results.
	 */
	tcase_set_timeout(tcase, 50);

	suite_add_tcase(suite, tcase);

	/*
	 * The rejection tests are noisy, so they get their own case with a
	 * fixture that mutes the log and puts it back however the test ends.
	 */
	tcase_add_checked_fixture(tcase_errors, _quiet_setup, _quiet_teardown);
	tcase_add_test(tcase_errors, test_unpack_data_key_lengths);
	tcase_add_test(tcase_errors, test_unpack_data_malformed);
	tcase_add_test(tcase_errors, test_unpack_data_too_deep);
	tcase_add_test(tcase_errors, test_unpack_data_too_many);
	tcase_add_test(tcase_errors, test_unpack_data_garbage);

	tcase_set_timeout(tcase_errors, 50);

	suite_add_tcase(suite, tcase_errors);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}

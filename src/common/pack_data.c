/*****************************************************************************\
 *  pack_data.c - pack and unpack a data_t
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

#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/pack_data.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

strong_alias(pack_data, slurm_pack_data);
strong_alias(unpack_data, slurm_unpack_data);

/*
 * Wire tags for a packed data_t, one byte each.
 *
 * A tag names both the type and the width of what follows it, so the suffix
 * says which of pack8(), pack16(), pack32() or pack64() wrote the value. A
 * list, a dictionary and an integer each have one tag per width, so that a
 * small one does not pay for a size it never needs; the rest have a single
 * width each. An integer is signed, so a narrowed one is sign extended on the
 * way back out.
 *
 * A string has one tag whatever its size, because the run of bytes behind it
 * carries its own length; see _pack_der_bytes(). A dictionary key is that
 * same run of bytes without a tag in front of it.
 *
 * 0x00 and 0xff are reserved rather than assigned, so that a zeroed or a
 * saturated buffer is refused on its first byte instead of being read as a
 * value.
 *
 * Append only, and never renumber: these go on the wire.
 */
typedef enum {
	/* Tag 0x00 is reserved and should not be used */
	DATA_TAG_RESERVED = 0x00,
	DATA_TAG_NULL = 0x01,
	DATA_TAG_LIST_8 = 0x02,
	DATA_TAG_LIST_16 = 0x03,
	DATA_TAG_LIST_32 = 0x04,
	DATA_TAG_DICT_8 = 0x05,
	DATA_TAG_DICT_16 = 0x06,
	DATA_TAG_DICT_32 = 0x07,
	DATA_TAG_INT_8 = 0x08,
	DATA_TAG_INT_16 = 0x09,
	DATA_TAG_INT_32 = 0x0a,
	DATA_TAG_INT_64 = 0x0b,
	DATA_TAG_STRING = 0x0c,
	DATA_TAG_FLOAT_64 = 0x0d,
	DATA_TAG_BOOL_8 = 0x0e,
	/* Tag 0xff is reserved and should not be used */
	DATA_TAG_RESERVED2 = 0xff,
} data_pack_tag_t;

/*
 * Nesting accepted by pack_data() and unpack_data().
 *
 * Each level costs only the two bytes of a tag and a count on the wire, so
 * without a limit a small message can drive unpack_data() deep enough to run
 * the stack out. Sixteen is deeper than any payload this packs in practice,
 * while still bounding the recursion well short of the stack.
 *
 * pack_data() holds to the same limit, so that anything it writes can be read
 * back. A caller that packs deeper gets an error rather than a buffer that
 * only fails later, on the far side of the wire.
 */
#define MAX_PACK_DATA_DEPTH (16)

/*
 * Entries accepted from one message.
 *
 * The depth limit bounds how deep a message goes, not how wide. Every entry
 * costs a data_t and a list node, which data.c takes with xmalloc() and so
 * cannot refuse, and one wire byte is enough to ask for another. Without a
 * ceiling a small message turns into a large heap and then an abort that no
 * caller can catch, which is the opposite of what _unpack_der_bytes() does
 * with a length it cannot meet.
 *
 * The ceiling also has to hold the cost of throwing the entries away again.
 * data.c releases a list by walking it to find each node's predecessor, so
 * freeing is quadratic: eight thousand entries take about a tenth of a second
 * to release, this ceiling takes about two, and sixty five thousand would
 * take seven. A ceiling high enough to matter for the heap would hand back a
 * processor instead.
 *
 * This is far more than any payload this packs, and holds the entries from
 * one message to about four megabytes.
 *
 * pack_data() holds to the same ceiling, for the reason the depth limit does:
 * so that it cannot write a buffer unpack_data() is bound to refuse.
 */
#define MAX_PACK_DATA_NODES (32768)

#define PACK_DATA_ARGS_MAGIC 0x0a2b3c4d

typedef struct {
	int magic; /* PACK_DATA_ARGS_MAGIC */
	buf_t *buffer;
	uint16_t protocol_version;
	int depth; /* nesting of the entries, not of the container */
	uint32_t *nodes; /* entries taken so far, shared by the whole message */
	int rc;
} pack_data_args_t;

static int _pack_data(const data_t *val, uint16_t protocol_version,
		      buf_t *buffer, int depth, uint32_t *nodes);
static int _unpack_data(data_t *valp, uint16_t protocol_version, buf_t *buffer,
			int depth, uint32_t *nodes);

/*
 * Count one entry against the budget for this message.
 * IN/OUT nodes - entries taken so far, raised by one
 * IN caller - function to name in the error
 * RET SLURM_SUCCESS, or SLURM_ERROR when the message asks for too many
 */
static int _check_data_nodes(uint32_t *nodes, const char *caller)
{
	if ((*nodes)++ < MAX_PACK_DATA_NODES)
		return SLURM_SUCCESS;

	error("%s: more than %d entries in one message", caller,
	      MAX_PACK_DATA_NODES);
	return SLURM_ERROR;
}

/*
 * Check that a container may be nested here at all.
 * IN depth - nesting of the value holding the container
 * IN caller - function to name in the error
 * RET SLURM_SUCCESS, or SLURM_ERROR when it is nested too deeply
 */
static int _check_data_depth(int depth, const char *caller)
{
	if (depth < MAX_PACK_DATA_DEPTH)
		return SLURM_SUCCESS;

	error("%s: nested deeper than %d", caller, MAX_PACK_DATA_DEPTH);
	return SLURM_ERROR;
}

/*
 * Pack one entry of a data_t list.
 * IN data - entry to pack
 * IN/OUT arg - pack_data_args_t holding the buffer to append to, and taking
 *	the error from a failed entry
 * RET DATA_FOR_EACH_CONT, or DATA_FOR_EACH_FAIL with arg->rc set
 */
static data_for_each_cmd_t _pack_data_list_entry(const data_t *data, void *arg)
{
	pack_data_args_t *args = arg;

	xassert(args->magic == PACK_DATA_ARGS_MAGIC);

	if ((args->rc = _check_data_nodes(args->nodes, __func__)))
		return DATA_FOR_EACH_FAIL;

	if ((args->rc = _pack_data(data, args->protocol_version, args->buffer,
				   args->depth, args->nodes)))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

/*
 * Pack the length of a run of bytes, in the definite length form of X.690.
 *
 * The length says its own size, so neither a string tag nor a dictionary key
 * has to name a width. A first byte below 0x80 is the length itself; at or
 * above it, the low bits count the length bytes that follow, most significant
 * first. Nearly every string and key is short, so this is one byte where a
 * fixed uint32_t spent four.
 *
 *   key length               first   follows  field  example
 *   -----------------------  ------  -------  -----  --------------
 *   0 .. 0x7f                length        0      1  7f
 *   0x80 .. 0xff             0x81          1      2  81 80
 *   0x100 .. 0xffff          0x82          2      3  82 01 00
 *   0x10000 .. 0xffffff      0x83          3      4  83 01 00 00
 *   0x1000000 .. 0xfffffffd  0x84          4      5  84 01 00 00 00
 *
 * Only the shortest form of a length is written, and only the shortest form is
 * accepted, so one length has one representation on the wire. That is why 0x83
 * is here: without it a length of 0x10000 would have no form to be shortest
 * in.
 *
 * This holds for lengths alone. A container count and an integer are packed at
 * the narrowest width that holds them but a wider one is still read, so a
 * message from a peer that chose differently unpacks and packs back to fewer
 * bytes than arrived. Do not treat what pack_data() writes as an identity for
 * a value: it is not a hash, a dedup key, or something to compare a signature
 * against.
 *
 * IN len - number of bytes, which the caller has already bounded
 * IN/OUT buffer - buffer to append the length to
 */
static void _pack_der_len(uint32_t len, buf_t *buffer)
{
	if (len <= 0x7f) {
		pack8(len, buffer);
	} else if (len <= 0xff) {
		pack8(0x81, buffer);
		pack8(len, buffer);
	} else if (len <= 0xffff) {
		pack8(0x82, buffer);
		pack16(len, buffer);
	} else if (len <= 0xffffff) {
		pack8(0x83, buffer);
		pack8((len >> 16), buffer);
		pack16(len, buffer);
	} else {
		pack8(0x84, buffer);
		pack32(len, buffer);
	}
}

/*
 * Unpack a length written by _pack_der_len().
 *
 * Four bytes is the widest form accepted, so a length always fits a uint32.
 * The indefinite form, and any length not written in its shortest form, are
 * refused: they are not what this packs, and accepting them would give one
 * length several spellings on the wire.
 *
 * OUT len - number of bytes read
 * IN/OUT buffer - buffer to read from
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
static int _unpack_der_len(uint32_t *len, buf_t *buffer)
{
	uint8_t first = 0;
	uint32_t val = 0;
	int bytes = 0;

	safe_unpack8(&first, buffer);

	if (first <= 0x7f) {
		*len = first;
		return SLURM_SUCCESS;
	}

	bytes = (first & 0x7f);

	/* 0x80 is the indefinite form, and past four bytes will not fit */
	if (!bytes || (bytes > 4)) {
		error("%s: invalid length form 0x%02x", __func__, first);
		return SLURM_ERROR;
	}

	for (int i = 0; i < bytes; i++) {
		uint8_t b = 0;

		safe_unpack8(&b, buffer);
		val = ((val << 8) | b);
	}

	if ((val <= 0x7f) || (val < (1U << (8 * (bytes - 1))))) {
		error("%s: length %u is not in its shortest form",
		      __func__, val);
		return SLURM_ERROR;
	}

	/*
	 * The sentinels every other uint32_t on the wire uses are reserved
	 * here rather than being sizes, so that they stay available to mean
	 * something later. This is a rejection rather than an xassert()
	 * because val came off the wire: an assert would abort the daemon on
	 * a peer's say so, and would leave the reservation unenforced in a
	 * build that defines NDEBUG.
	 */
	if ((val == INFINITE) || (val == NO_VAL)) {
		error("%s: length 0x%x is reserved", __func__, val);
		return SLURM_ERROR;
	}

	*len = val;
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

/*
 * Read a run of bytes as a NUL terminated string.
 *
 * One byte more than the wire says is allocated, because the terminator is
 * not sent and data_t holds a string as a char *. try_xmalloc() clears what
 * it hands back, so the extra byte is already the terminator; the store below
 * only says so. A zero length is an empty string rather than a NULL, so a
 * string tag always unpacks to a string.
 *
 * The length came off the wire and is only bounded by MAX_PACK_MEM_LEN, so a
 * peer can ask for a gigabyte here. try_xmalloc() gives back NULL when that
 * cannot be met, which refuses the message rather than killing the daemon
 * the way xmalloc() would.
 *
 * Not safe_unpackstr(), which reads a four byte length and, when
 * slurmdbd_conf is set, escapes what it read. That would make what comes back
 * depend on which daemon is reading, and this format must read the same
 * everywhere.
 *
 * OUT str - the string, which is empty when the length is zero
 * IN/OUT buffer - buffer to read from
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
static int _unpack_der_bytes(char **str, buf_t *buffer)
{
	char *out = NULL;
	uint32_t len = 0;

	xassert(!*str);

	if (_unpack_der_len(&len, buffer))
		return SLURM_ERROR;

	if (!len) {
		if (!(*str = try_xmalloc(1))) {
			error("%s: unable to allocate an empty string",
			      __func__);
			return SLURM_ERROR;
		}

		return SLURM_SUCCESS;
	}

	if (len > MAX_PACK_MEM_LEN) {
		error("%s: string too large to unpack (%u > %u)", __func__,
		      len, MAX_PACK_MEM_LEN);
		return SLURM_ERROR;
	}

	/*
	 * Check the bytes are there before asking for memory to hold them, as
	 * unpackstr_xmalloc() and the rest of pack.c do. Otherwise a handful
	 * of bytes claiming a gigabyte reserves a gigabyte, only to be thrown
	 * away when the copy below finds the buffer empty.
	 */
	if (remaining_buf(buffer) < len) {
		error("%s: %u bytes claimed but only %u remain", __func__, len,
		      remaining_buf(buffer));
		return SLURM_ERROR;
	}

	if (!(out = try_xmalloc(len + 1))) {
		error("%s: unable to allocate %u bytes for a string",
		      __func__, (len + 1));
		return SLURM_ERROR;
	}

	if (unpackmem_array(out, len, buffer)) {
		xfree(out);
		return SLURM_ERROR;
	}

	out[len] = '\0';

	/*
	 * The terminator is not sent, so a NUL among these bytes was not put
	 * there by pack_data(). Left in, it would truncate the string at the
	 * NUL, so what came off the wire and what the caller sees would
	 * differ, and two dictionary keys that differ only past a NUL would
	 * collide on the shorter of the two.
	 */
	if (strnlen(out, len) != len) {
		error("%s: %u bytes hold a NUL at %zu", __func__, len,
		      strnlen(out, len));
		xfree(out);
		return SLURM_ERROR;
	}

	*str = out;

	return SLURM_SUCCESS;
}

/*
 * Pack a run of bytes behind its own length.
 *
 * Note: DER (Distinguished Encoding Rules) is part of the Abstract Syntax
 * Notation One (ASN.1) standard.
 *
 * This is what a string value and a dictionary key both are on the wire. A
 * string puts DATA_TAG_STRING in front of it; a key has nothing in front of
 * it, because the dictionary tag already said an entry follows.
 *
 * The length counts characters, not the terminator: the NUL is not sent and
 * the reader supplies it. An empty string is a zero length and no bytes.
 *
 * IN str - bytes to pack, which may be NULL only when size_val is 0
 * IN size_val - how many bytes, not counting the terminator
 * IN/OUT buffer - buffer to append the length and bytes to
 */
static void _pack_der_bytes(char *str, uint32_t size_val, buf_t *buffer)
{
	_pack_der_len(size_val, buffer);

	/* an empty string is the length alone, with no bytes behind it */
	if (size_val)
		packmem_array(str, size_val, buffer);
}

/*
 * Pack one key and value of a data_t dictionary.
 * IN key - entry key, packed as a string ahead of the value
 * IN data - entry value to pack
 * IN/OUT arg - pack_data_args_t holding the buffer to append to, and taking
 *	the error from a failed entry
 * RET DATA_FOR_EACH_CONT, or DATA_FOR_EACH_FAIL with arg->rc set
 */
static data_for_each_cmd_t _pack_data_dict_entry(const char *key,
						 const data_t *data, void *arg)
{
	pack_data_args_t *args = arg;
	/* packmem_array() takes a non-const pointer */
	char *str = (char *) key;
	/* size_t, not uint32_t: the compare must not truncate first */
	const size_t size_val = (str ? strlen(str) : 0);

	xassert(args->magic == PACK_DATA_ARGS_MAGIC);

	if ((args->rc = _check_data_nodes(args->nodes, __func__)))
		return DATA_FOR_EACH_FAIL;

	if (size_val > MAX_PACK_MEM_LEN) {
		error("%s: key too large to pack (%zu > %u)",
		      __func__, size_val, MAX_PACK_MEM_LEN);
		args->rc = ESLURM_DATA_TOO_LARGE;
		return DATA_FOR_EACH_FAIL;
	}

	_pack_der_bytes(str, size_val, args->buffer);

	if ((args->rc = _pack_data(data, args->protocol_version, args->buffer,
				   args->depth, args->nodes)))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

/*
 * Pack an entry count at the narrowest width that holds it, behind the tag
 * that says which width that was.
 * IN count - number of entries; the caller must have checked it fits uint32_t
 * IN tag8 - tag to pack when the count fits a uint8_t
 * IN tag16 - tag to pack when the count fits a uint16_t
 * IN tag32 - tag to pack otherwise
 * IN/OUT buffer - buffer to append the tag and count to
 */
static void _pack_data_count(size_t count, data_pack_tag_t tag8,
			     data_pack_tag_t tag16, data_pack_tag_t tag32,
			     buf_t *buffer)
{
	if (count <= UINT8_MAX) {
		pack8(tag8, buffer);
		pack8(count, buffer);
	} else if (count <= UINT16_MAX) {
		pack8(tag16, buffer);
		pack16(count, buffer);
	} else {
		/* the callers refuse a count this large, so it cannot arrive */
		xassert(count < UINT32_MAX);
		pack8(tag32, buffer);
		pack32(count, buffer);
	}
}

/*
 * Pack a list, its entry count ahead of its entries.
 * IN val - list to pack
 * IN/OUT buffer - buffer to append the tag, count and entries to
 * IN depth - nesting of the value holding this list
 * RET SLURM_SUCCESS or an error
 */
static int _pack_data_list(const data_t *val, uint16_t protocol_version,
			   buf_t *buffer, int depth, uint32_t *nodes)
{
	pack_data_args_t args = {
		.magic = PACK_DATA_ARGS_MAGIC,
		.buffer = buffer,
		.protocol_version = protocol_version,
		.depth = (depth + 1),
		.nodes = nodes,
		.rc = SLURM_SUCCESS,
	};
	const size_t count = data_get_list_length(val);
	int rc = EINVAL;

	if ((rc = _check_data_depth(depth, __func__)))
		return rc;

	if (count >= UINT32_MAX) {
		error("%s: list too long to pack (%zu >= %u)",
		      __func__, count, UINT32_MAX);
		return ESLURM_DATA_TOO_LARGE;
	}

	_pack_data_count(count, DATA_TAG_LIST_8, DATA_TAG_LIST_16,
			 DATA_TAG_LIST_32, buffer);

	if (data_list_for_each_const(val, _pack_data_list_entry, &args) < 0)
		return args.rc ? args.rc : SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Pack a dictionary, its entry count ahead of its keys and values.
 * IN val - dictionary to pack
 * IN/OUT buffer - buffer to append the tag, count and entries to
 * IN depth - nesting of the value holding this dictionary
 * RET SLURM_SUCCESS or an error
 */
static int _pack_data_dict(const data_t *val, uint16_t protocol_version,
			   buf_t *buffer, int depth, uint32_t *nodes)
{
	pack_data_args_t args = {
		.magic = PACK_DATA_ARGS_MAGIC,
		.buffer = buffer,
		.protocol_version = protocol_version,
		.depth = (depth + 1),
		.nodes = nodes,
		.rc = SLURM_SUCCESS,
	};
	const size_t count = data_get_dict_length(val);
	int rc = EINVAL;

	if ((rc = _check_data_depth(depth, __func__)))
		return rc;

	if (count >= UINT32_MAX) {
		error("%s: dictionary too large to pack (%zu >= %u)",
		      __func__, count, UINT32_MAX);
		return ESLURM_DATA_TOO_LARGE;
	}

	_pack_data_count(count, DATA_TAG_DICT_8, DATA_TAG_DICT_16,
			 DATA_TAG_DICT_32, buffer);

	if (data_dict_for_each_const(val, _pack_data_dict_entry, &args) < 0)
		return args.rc ? args.rc : SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Pack an integer at the narrowest width that holds it.
 * IN val - integer to pack
 * IN/OUT buffer - buffer to append the tag and value to
 * RET SLURM_SUCCESS
 */
static int _pack_data_int(const data_t *val, uint16_t protocol_version,
			  buf_t *buffer)
{
	int64_t i = data_get_int(val);

	/*
	 * The casts keep the two's complement bit pattern, which
	 * unpack_data() sign extends back to int64_t.
	 */
	if ((i >= INT8_MIN) && (i <= INT8_MAX)) {
		pack8(DATA_TAG_INT_8, buffer);
		pack8((uint8_t) (int8_t) i, buffer);
	} else if ((i >= INT16_MIN) && (i <= INT16_MAX)) {
		pack8(DATA_TAG_INT_16, buffer);
		pack16((uint16_t) (int16_t) i, buffer);
	} else if ((i >= INT32_MIN) && (i <= INT32_MAX)) {
		pack8(DATA_TAG_INT_32, buffer);
		pack32((uint32_t) (int32_t) i, buffer);
	} else {
		pack8(DATA_TAG_INT_64, buffer);
		pack64((uint64_t) i, buffer);
	}

	return SLURM_SUCCESS;
}

/*
 * Pack a string.
 * IN val - string to pack
 * IN/OUT buffer - buffer to append the tag and value to
 * RET SLURM_SUCCESS, or ESLURM_DATA_TOO_LARGE when it will not fit
 */
static int _pack_data_string(const data_t *val, uint16_t protocol_version,
			     buf_t *buffer)
{
	/* packmem_array() takes a non-const pointer */
	char *str = (char *) data_get_string(val);
	/* size_t, not uint32_t: the compare must not truncate first */
	const size_t size_val = (str ? strlen(str) : 0);

	/*
	 * Not packstr(), which always spends four bytes on the length and
	 * only logs when the string is too long, leaving the tag written with
	 * nothing after it. The length goes out at the width the tag names,
	 * and the caller is told when it will not fit.
	 */
	if (size_val > MAX_PACK_MEM_LEN) {
		error("%s: string too large to pack (%zu > %u)", __func__,
		      size_val, MAX_PACK_MEM_LEN);
		return ESLURM_DATA_TOO_LARGE;
	}

	pack8(DATA_TAG_STRING, buffer);
	_pack_der_bytes(str, size_val, buffer);

	return SLURM_SUCCESS;
}

/*
 * Pack one data_t, tracking how deeply it is nested.
 * IN val - data to pack
 * IN/OUT buffer - buffer to append the tag and value to
 * IN depth - nesting of this value, 0 at the outermost
 * RET SLURM_SUCCESS or an error
 */
static int _pack_data(const data_t *val, uint16_t protocol_version,
		      buf_t *buffer, int depth, uint32_t *nodes)
{
	data_type_t type = DATA_TYPE_NONE;

	if (!val || !buffer)
		return EINVAL;

	type = data_get_type(val);

	switch (type) {
	case DATA_TYPE_NULL:
		pack8(DATA_TAG_NULL, buffer);
		return SLURM_SUCCESS;
	case DATA_TYPE_LIST:
		return _pack_data_list(val, protocol_version, buffer, depth,
				       nodes);
	case DATA_TYPE_DICT:
		return _pack_data_dict(val, protocol_version, buffer, depth,
				       nodes);
	case DATA_TYPE_INT_64:
		return _pack_data_int(val, protocol_version, buffer);
	case DATA_TYPE_STRING:
		return _pack_data_string(val, protocol_version, buffer);
	case DATA_TYPE_FLOAT:
		pack8(DATA_TAG_FLOAT_64, buffer);
		packdouble(data_get_float(val), buffer);
		return SLURM_SUCCESS;
	case DATA_TYPE_BOOL:
		pack8(DATA_TAG_BOOL_8, buffer);
		packbool(data_get_bool(val), buffer);
		return SLURM_SUCCESS;
	case DATA_TYPE_NONE:
		/* fall through */
	case DATA_TYPE_MAX:
		fatal_abort("should never happen");
	}

	fatal_abort("should never happen");
}

extern int pack_data(const data_t *val, uint16_t protocol_version,
		     buf_t *buffer)
{
	uint32_t nodes = 0;

	/*
	 * Nothing in the format depends on the version yet. Check it is set
	 * so that a caller which never passed one is found now, rather than
	 * when a later version starts reading it.
	 */
	xassert(protocol_version);

	return _pack_data(val, protocol_version, buffer, 0, &nodes);
}

/*
 * Unpack the entries of a list.
 * IN/OUT valp - data to populate as a list
 * IN/OUT buffer - buffer to read from
 * IN tag - container tag already read from the buffer
 * IN depth - nesting of the value holding this list
 * RET SLURM_SUCCESS or an error
 */
static int _unpack_data_list(data_t *valp, uint16_t protocol_version,
			     buf_t *buffer, uint8_t tag, int depth,
			     uint32_t *nodes)
{
	uint32_t count = 0;
	int rc = EINVAL;

	if ((rc = _check_data_depth(depth, __func__)))
		return rc;

	switch (tag) {
	case DATA_TAG_LIST_8:
	{
		uint8_t c = 0;

		safe_unpack8(&c, buffer);
		count = c;
		break;
	}
	case DATA_TAG_LIST_16:
	{
		uint16_t c = 0;

		safe_unpack16(&c, buffer);
		count = c;
		break;
	}
	case DATA_TAG_LIST_32:
		safe_unpack32(&count, buffer);
		break;
	default:
		/* the caller only ever hands this a list tag */
		fatal_abort("should never happen");
	}

	(void) data_set_list(valp);

	for (uint32_t i = 0; i < count; i++) {
		int rc = EINVAL;

		if ((rc = _check_data_nodes(nodes, __func__)))
			return rc;

		if ((rc = _unpack_data(data_list_append(valp), protocol_version,
				       buffer, (depth + 1), nodes)))
			return rc;
	}

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

/*
 * Unpack the entries of a dictionary.
 * IN/OUT valp - data to populate as a dictionary
 * IN/OUT buffer - buffer to read from
 * IN tag - container tag already read from the buffer
 * IN depth - nesting of the value holding this dictionary
 * RET SLURM_SUCCESS or an error
 */
static int _unpack_data_dict(data_t *valp, uint16_t protocol_version,
			     buf_t *buffer, uint8_t tag, int depth,
			     uint32_t *nodes)
{
	uint32_t count = 0;
	int rc = EINVAL;

	if ((rc = _check_data_depth(depth, __func__)))
		return rc;

	switch (tag) {
	case DATA_TAG_DICT_8:
	{
		uint8_t c = 0;

		safe_unpack8(&c, buffer);
		count = c;
		break;
	}
	case DATA_TAG_DICT_16:
	{
		uint16_t c = 0;

		safe_unpack16(&c, buffer);
		count = c;
		break;
	}
	case DATA_TAG_DICT_32:
		safe_unpack32(&count, buffer);
		break;
	default:
		/* the caller only ever hands this a dictionary tag */
		fatal_abort("should never happen");
	}

	(void) data_set_dict(valp);

	for (uint32_t i = 0; i < count; i++) {
		int rc = EINVAL;
		char *key = NULL;
		data_t *entry = NULL;

		if ((rc = _check_data_nodes(nodes, __func__)))
			return rc;

		/* a key is a run of bytes behind its own length, and no tag */
		if (_unpack_der_bytes(&key, buffer))
			return SLURM_ERROR;

		entry = data_key_set(valp, key);
		xfree(key);

		if ((rc = _unpack_data(entry, protocol_version, buffer,
				       (depth + 1), nodes)))
			return rc;
	}

	/*
	 * data_key_set() hands back the entry a key already names, so a
	 * repeated key overwrites rather than adding. The count is the only
	 * witness that it happened: fewer entries than the wire declared
	 * means two of its keys were the same, and the value that arrived
	 * first is already gone.
	 */
	if (data_get_dict_length(valp) != count) {
		error("%s: %u entries declared but %zu distinct keys",
		      __func__, count, data_get_dict_length(valp));
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

/*
 * Unpack a string, which is a run of bytes behind its own length.
 *
 * IN/OUT valp - data to populate as a string
 * IN protocol_version - version of the peer this was packed by
 * IN/OUT buffer - buffer to read from
 * RET SLURM_SUCCESS or an error
 */
static int _unpack_data_string(data_t *valp, uint16_t protocol_version,
			       buf_t *buffer)
{
	char *str = NULL;

	if (_unpack_der_bytes(&str, buffer))
		return SLURM_ERROR;

	/* a zero length is an empty string, never a NULL */
	data_set_string_own(valp, str);

	return SLURM_SUCCESS;
}

/*
 * Unpack one data_t, tracking how deeply it is nested.
 * IN/OUT valp - existing data_t to populate
 * IN/OUT buffer - buffer to read from
 * IN depth - nesting of this value, 0 at the outermost
 * RET SLURM_SUCCESS or an error
 */
static int _unpack_data(data_t *valp, uint16_t protocol_version, buf_t *buffer,
			int depth, uint32_t *nodes)
{
	uint8_t tag = 0;

	safe_unpack8(&tag, buffer);

	switch (tag) {
	case DATA_TAG_NULL:
		data_set_null(valp);
		return SLURM_SUCCESS;
	case DATA_TAG_LIST_8:
	case DATA_TAG_LIST_16:
	case DATA_TAG_LIST_32:
		return _unpack_data_list(valp, protocol_version, buffer, tag,
					 depth, nodes);
	case DATA_TAG_DICT_8:
	case DATA_TAG_DICT_16:
	case DATA_TAG_DICT_32:
		return _unpack_data_dict(valp, protocol_version, buffer, tag,
					 depth, nodes);
	case DATA_TAG_INT_8:
	{
		uint8_t i = 0;

		safe_unpack8(&i, buffer);
		/* the cast sign extends back to int64_t */
		data_set_int(valp, (int8_t) i);
		return SLURM_SUCCESS;
	}
	case DATA_TAG_INT_16:
	{
		uint16_t i = 0;

		safe_unpack16(&i, buffer);
		data_set_int(valp, (int16_t) i);
		return SLURM_SUCCESS;
	}
	case DATA_TAG_INT_32:
	{
		uint32_t i = 0;

		safe_unpack32(&i, buffer);
		data_set_int(valp, (int32_t) i);
		return SLURM_SUCCESS;
	}
	case DATA_TAG_INT_64:
	{
		uint64_t i = 0;

		safe_unpack64(&i, buffer);
		data_set_int(valp, (int64_t) i);
		return SLURM_SUCCESS;
	}
	case DATA_TAG_STRING:
		return _unpack_data_string(valp, protocol_version, buffer);
	case DATA_TAG_FLOAT_64:
	{
		double d = 0;

		safe_unpackdouble(&d, buffer);
		data_set_float(valp, d);
		return SLURM_SUCCESS;
	}
	case DATA_TAG_BOOL_8:
	{
		bool b = false;

		safe_unpackbool(&b, buffer);
		data_set_bool(valp, b);
		return SLURM_SUCCESS;
	}
	default:
		/*
		 * The tag was read out of the buffer, so one that is not ours
		 * is untrusted input rather than anything wrong with this
		 * process. Reject the message instead of aborting on it.
		 */
		error("%s: invalid data tag 0x%02x", __func__, tag);
		return SLURM_ERROR;
	}

unpack_error:
	return SLURM_ERROR;
}

extern int unpack_data(data_t *valp, uint16_t protocol_version, buf_t *buffer)
{
	uint32_t nodes = 0;

	/* see pack_data() */
	xassert(protocol_version);

	if (!valp || !buffer)
		return EINVAL;

	return _unpack_data(valp, protocol_version, buffer, 0, &nodes);
}

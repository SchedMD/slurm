/*****************************************************************************\
 *  serializer_url_encoded.c - Serializer for url-encoded.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "slurm/slurm.h"

#include "src/common/slurm_xlator.h"
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Serializer URL encoded plugin";
const char plugin_type[] = "serializer/url-encoded";
const uint32_t  plugin_version = SLURM_VERSION_NUMBER;
const char *mime_types[] = {
	"application/x-www-form-urlencoded",
	NULL
};

static bool _is_char_hex(char buffer)
{
	return (buffer >= '0' && buffer <= '9') ||
	       (buffer >= 'a' && buffer <= 'f') ||
	       (buffer >= 'A' && buffer <= 'F');
}

extern int serializer_p_serialize(char **dest, const data_t *data,
				  data_serializer_flags_t flags)
{
	return ESLURM_NOT_SUPPORTED;
}

static int _handle_new_key_char(data_t *d, char **key, char **buffer,
				bool convert_types)
{
	if (*key == NULL && *buffer == NULL) {
		/* example: &test=value */
	} else if (*key == NULL && *buffer != NULL) {
		/*
		 * example: test&test=value
		 * existing buffer, assume null value.
		 */
		data_t *c = data_key_set(d, *buffer);
		data_set_null(c);
		xfree(*buffer);
		*buffer = NULL;
	} else if (*key != NULL && *buffer == NULL) {
		/* example: &test1=&=value */
		data_t *c = data_key_set(d, *key);
		data_set_null(c);
		xfree(*key);
		*key = NULL;
	} else if (*key != NULL && *buffer != NULL) {
		data_t *c = data_key_set(d, *key);
		data_set_string(c, *buffer);

		if (convert_types)
			(void) data_convert_type(c, DATA_TYPE_NONE);

		xfree(*key);
		xfree(*buffer);
		*key = NULL;
		*buffer = NULL;
	}

	return SLURM_SUCCESS;
}

/*
 * chars that can pass without decoding.
 * rfc3986: unreserved characters.
 */
static bool _is_valid_url_char(char buffer)
{
	return (buffer >= '0' && buffer <= '9') ||
	       (buffer >= 'a' && buffer <= 'z') ||
	       (buffer >= 'A' && buffer <= 'Z') || buffer == '~' ||
	       buffer == '-' || buffer == '.' || buffer == '_';
}

/*
 * decodes % sequence.
 * IN ptr pointing to % character
 * RET \0 on error or decoded character
 */
static unsigned char _decode_seq(const char *ptr)
{
	if (_is_char_hex(*(ptr + 1)) && _is_char_hex(*(ptr + 2))) {
		/* using unsigned char to avoid any rollover */
		unsigned char high = *(ptr + 1);
		unsigned char low = *(ptr + 2);
		unsigned char decoded = (slurm_char_to_hex(high) << 4) +
					slurm_char_to_hex(low);

		//TODO: find more invalid characters?
		if (decoded == '\0') {
			error("%s: invalid URL escape sequence for 0x00",
			      __func__);
			return '\0';
		} else if (decoded == 0xff) {
			error("%s: invalid URL escape sequence for 0xff",
			      __func__);
			return '\0';
		}

		debug5("%s: URL decoded: 0x%c%c -> %c",
		       __func__, high, low, decoded);

		return decoded;
	} else {
		debug("%s: invalid URL escape sequence: %s", __func__, ptr);
		return '\0';
	}
}

/*
 * Parses url query into a data struct.
 * IN dest - ptr to data to overwrite on success
 * IN src rfc3986&rfc1866 query string
 * 	application/x-www-form-urlencoded
 * 	breaks key=value&key2=value2&...
 * 	into a data_t dictionary
 * 	dup keys will override existing keys
 * IN len - not used
 * RET SLURM_SUCCESS or error
 */
extern int serializer_p_deserialize(data_t **dest, const char *src,
				    size_t len)
{
	int rc = SLURM_SUCCESS;
	data_t *d = data_set_dict(data_new());
	char *key = NULL;
	char *buffer = NULL;

	/* extract each word */
	for (const char *ptr = src; ptr && !rc && *ptr != '\0'; ++ptr) {
		if (_is_valid_url_char(*ptr)) {
			xstrcatchar(buffer, *ptr);
			continue;
		}

		switch (*ptr) {
		case '%': /* rfc3986 */
		{
			const char c = _decode_seq(ptr);
			if (c != '\0') {
				/* shift past the hex value */
				ptr += 2;

				xstrcatchar(buffer, c);
			} else {
				debug("%s: invalid URL escape sequence: %s",
				      __func__, ptr);
				rc = SLURM_ERROR;
				break;
			}
			break;
		}
		case '+': /* rfc1866 only */
			xstrcatchar(buffer, ' ');
			break;
		case ';': /* rfc1866 requests ';' treated like '&' */
		case '&': /* rfc1866 only */
			rc = _handle_new_key_char(d, &key, &buffer, true);
			break;
		case '=': /* rfc1866 only */
			if (key == NULL && buffer == NULL) {
				/* example: =test=value */
				error("%s: invalid url character = before key name",
				      __func__);
				rc = SLURM_ERROR;
			} else if (key == NULL && buffer != NULL) {
				key = buffer;
				buffer = NULL;
			} else if (key != NULL && buffer == NULL) {
				/* example: test===value */
				debug4("%s: ignoring duplicate character = in url",
				       __func__);
			} else if (key != NULL && buffer != NULL) {
				/* example: test=value=testv */
				error("%s: invalid url character = before new key name",
				      __func__);
				rc = SLURM_ERROR;
			}
			break;
		default:
			debug("%s: unexpected URL character: %c",
			      __func__, *ptr);
			rc = SLURM_ERROR;
		}
	}

	/* account for last entry */
	if (!rc)
		rc = _handle_new_key_char(d, &key, &buffer, true);
	if (!rc && buffer)
		/* account for last entry not having a value */
		rc = _handle_new_key_char(d, &key, &buffer, true);

	xassert(rc || !buffer);
	xassert(rc || !key);

	xfree(buffer);
	xfree(key);

	if (rc) {
		FREE_NULL_DATA(d);
	} else {
		FREE_NULL_DATA(*dest);
		*dest = d;
	}

	return rc;
}

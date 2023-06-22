/*****************************************************************************\
 *  pem_key.c - Build a PEM-formatted RSA public key from mod and exp
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Use the libjwt base64 functions despite them not being prototyped in <jwt.h>.
 * Otherwise we'd probably end up copying the exact same implementation
 * (the implementation they use is originally from Apache and is BSD licensed)
 * into Slurm rather than pick up another external dependency.
 */
extern int jwt_Base64decode(unsigned char *bufplain, const char *bufcoded);
extern int jwt_Base64encode(char *encoded, const char *string, int len);

/*
 * If the first hex character is '8', 'a', 'b', 'c', 'd', or 'e',
 * prepend with an extra zero byte ("00" in hex).
 */
static void _handle_prepend(char **string)
{
	char *p = NULL;

	if (**string > '7') {
		xstrfmtcat(p, "00%s", *string);
		xfree(*string);
		*string = p;
	}
}

static char *_to_base64_from_base64url(char *in)
{
	char *out;
	int i;

	/* extra padding in case the padding was stripped off */
	out = xmalloc(strlen(in) + 3);

	for (i = 0; i < strlen(in); i++) {
		switch (in[i]) {
		case '-':
			out[i] = '+';
			break;
		case '_':
			out[i] = '/';
			break;
		default:
			out[i] = in[i];
		}
	}

	/*
	 * Fix padding in case it was trimmed.
	 * String length must be a multiple of 4.
	 */
	for (int padding = 4 - (i % 4); padding < 4 && padding; padding--)
		out[i++] = '=';

	return out;
}

/*
 * Convert base64url encoded value to DER formatted hex.
 * Returns an xmalloc()'d string.
 */
static char *_to_hex(char *base64url)
{
	char *base64, *hex;
	unsigned char *bin;
	int binlen;

	base64 = _to_base64_from_base64url(base64url);

	/*
	 * The binary format is ~33% smaller,
	 * but just make the buffer equal length.
	 */
	bin = xmalloc(strlen(base64));
	binlen = jwt_Base64decode(bin, base64);

	hex = xstring_bytes2hex(bin, binlen, NULL);

	/* Deal with DER formatting requirements */
	_handle_prepend(&hex);

	xfree(base64);
	xfree(bin);

	return hex;
}

/*
 * Convert a number into the hex representation.
 * Always return an even number of characters.
 */
static char *_hex(int len)
{
	char *hex = NULL, *padded = NULL;

	xstrfmtcat(hex, "%x", len);
	if (!(strlen(hex) % 2))
		return hex;

	/*
	 * Value must have an even number of characters, so prepend '0'.
	 */
	xstrfmtcat(padded, "0%s", hex);
	xfree(hex);
	return padded;
}

/*
 * Convert an integer into DER format.
 * 0x00 - 0x7f are direct values - the "short format".
 * Otherwise the "long format" is used. The first byte is the number of bytes
 * needed to represent the value OR'd with 0x80, followed by the value itself.
 */
static char *_int_to_der_hex(int len)
{
	char *encoded, *h = _hex(len);

	if (len <= 127)
		return h;

	encoded = _hex(128 + strlen(h) / 2);
	xstrcat(encoded, h);
	xfree(h);

	return encoded;
}

static int _to_bin(char **bin, char *hex)
{
	int len = strlen(hex) / 2;
	char *tmp = xmalloc(len);

	for (int i = 0; i < strlen(hex) - 1; i += 2) {
		tmp[i / 2] = slurm_char_to_hex(hex[i]) * 16;
		tmp[i / 2] += slurm_char_to_hex(hex[i + 1]);
	}

	*bin = tmp;

	return len;
}

/*
 * Generate a PEM-formatted public key file from a given modulus and exponent.
 *
 * This code is manually smashing together DER from hex characters,
 * and then constructing the certificate by translating that first
 * into binary and then into base64.
 *
 * It would be nice if a (non-OpenSSL) library were available to deal with
 * this, or if the x5c field were consistently populated in JWKS files, but
 * otherwise we're stuck with this.
 *
 * Inspired by, and key magic strings sourced from:
 * https://stackoverflow.com/questions/18835132/xml-to-pem-in-node-js
 *
 * mod and exp need to be input in base64url format
 */
extern char *pem_from_mod_exp(char *mod, char *exp)
{
	int modbytes, expbytes, binkeylen;
	char *modhex, *exphex;
	char *modhexlender, *exphexlender, *totallender;
	char *layer1 = NULL, *layer2 = NULL, *layer3 = NULL;
	char *layer1lender, *layer2lender;
	char *binkey, *base64key, *pem = NULL;

	if (!mod || !exp)
		fatal("%s: invalid JWKS file, missing mod and/or exp values",
		      __func__);

	modhex = _to_hex(mod);
	exphex = _to_hex(exp);

	modbytes = strlen(modhex) / 2;
	expbytes = strlen(exphex) / 2;

	modhexlender = _int_to_der_hex(modbytes);
	exphexlender = _int_to_der_hex(expbytes);
	totallender = _int_to_der_hex(2 + modbytes + expbytes
					+ strlen(modhexlender) / 2
					+ strlen(exphexlender) / 2);

	/*
	 * Construct DER formatted key in hex.
	 */

	/* Innermost */
	xstrcat(layer1, "0030");
	xstrcat(layer1, totallender);
	xstrcat(layer1, "02");
	xstrcat(layer1, modhexlender);
	xstrcat(layer1, modhex);
	xstrcat(layer1, "02");
	xstrcat(layer1, exphexlender);
	xstrcat(layer1, exphex);

	/* Wrap it again */
	layer1lender = _int_to_der_hex(strlen(layer1) / 2);
	xstrcat(layer2, "300d06092a864886f70d010101050003");
	xstrcat(layer2, layer1lender);
	xstrcat(layer2, layer1);

	/* And once more for good measure */
	layer2lender = _int_to_der_hex(strlen(layer2) / 2);
	xstrcat(layer3, "30");
	xstrcat(layer3, layer2lender);
	xstrcat(layer3, layer2);

	/* Convert the hex to binary */
	binkeylen = _to_bin(&binkey, layer3);

	/* And the binary into hex */
	base64key = xcalloc(2, binkeylen);
	jwt_Base64encode(base64key, binkey, binkeylen);

	xstrcat(pem, "-----BEGIN PUBLIC KEY-----\n");
	xstrcat(pem, base64key);
	xstrcat(pem, "\n-----END PUBLIC KEY-----\n");

	xfree(modhex);
	xfree(exphex);
	xfree(modhexlender);
	xfree(exphexlender);
	xfree(totallender);
	xfree(layer1);
	xfree(layer2);
	xfree(layer3);
	xfree(layer1lender);
	xfree(layer2lender);
	xfree(binkey);
	xfree(base64key);

	return pem;
}

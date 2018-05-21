/*****************************************************************************\
 *  acct_gather_energy_ipmi_config.h - declarations for reading ipmi.conf
 *****************************************************************************
 *  Copyright (C) 2012
 *  Written by Bull- Thomas Cadeau
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

#ifndef _IPMI_READ_CONFIG_H
#define _IPMI_READ_CONFIG_H

#include <ipmi_monitoring.h>

#define DEFAULT_IPMI_FREQ 30
#define DEFAULT_IPMI_USER "foousername"
#define DEFAULT_IPMI_VARIABLE IPMI_MONITORING_SENSOR_UNITS_WATTS

typedef struct slurm_ipmi_conf {
	/* Adjust/approach the consumption
	 * in function of time between ipmi update and read call */
	bool adjustment;
	/* Assume the BMC is the sensor owner no matter what.  This option
	 * works around motherboards that incorrectly indicate a non-BMC
	 * sensor owner (e.g. usually bridging is required).*/
	bool assume_bmc_owner;
	/* authentication type to use
	 *   IPMI_MONITORING_AUTHENTICATION_TYPE_NONE                  = 0x00,
	 *   IPMI_MONITORING_AUTHENTICATION_TYPE_STRAIGHT_PASSWORD_KEY = 0x01,
	 *   IPMI_MONITORING_AUTHENTICATION_TYPE_MD2                   = 0x02,
	 *   IPMI_MONITORING_AUTHENTICATION_TYPE_MD5                   = 0x03,
	 * Pass < 0 for default of IPMI_MONITORING_AUTHENTICATION_TYPE_MD5*/
	uint32_t authentication_type;
	/* Attempt to bridge sensors not owned by the BMC*/
	bool bridge_sensors;
	/* Cipher suite identifier to determine authentication, integrity,
	 * and confidentiality algorithms to use.
	 * Supported Cipher Suite IDs
	 * (Key: A - Authentication Algorithm
	 *       I - Integrity Algorithm
	 *       C - Confidentiality Algorithm)
	 *   0 - A = None; I = None; C = None
	 *   1 - A = HMAC-SHA1; I = None; C = None
	 *   2 - A = HMAC-SHA1; I = HMAC-SHA1-96; C = None
	 *   3 - A = HMAC-SHA1; I = HMAC-SHA1-96; C = AES-CBC-128
	 *   6 - A = HMAC-MD5; I = None; C = None
	 *   7 - A = HMAC-MD5; I = HMAC-MD5-128; C = None
	 *   8 - A = HMAC-MD5; I = HMAC-MD5-128; C = AES-CBC-128
	 *   11 - A = HMAC-MD5; I = MD5-128; C = None
	 *   12 - A = HMAC-MD5; I = MD5-128; C = AES-CBC-128
	 *   15 - A = HMAC-SHA256; I = None; C = None
	 *   16 - A = HMAC-SHA256; I = HMAC-SHA256-128; C = None
	 *   17 - A = HMAC-SHA256; I = HMAC-SHA256-128; C = AES-CBC-128
	 * Pass < 0 for default.of 3.*/
	uint32_t cipher_suite_id;
	/* Allow sensor readings to be read even if the event/reading type
	 * code for the sensor is not valid.  This option works around
	 * poorly defined (and arguably illegal) SDR records that list
	 * non-discrete sensor expectations along with discrete state
	 * conditions.*/
	bool discrete_reading;
	/* Use this driver device for the IPMI driver.*/
	char *driver_device;
	/* Options for IPMI configuration*/
	/* Use a specific in-band driver.
	 *   IPMI_MONITORING_DRIVER_TYPE_KCS      = 0x00,
	 *   IPMI_MONITORING_DRIVER_TYPE_SSIF     = 0x01,
	 *   IPMI_MONITORING_DRIVER_TYPE_OPENIPMI = 0x02,
	 *   IPMI_MONITORING_DRIVER_TYPE_SUNBMC   = 0x03,
	 *    Pass < 0 for default of IPMI_MONITORING_DRIVER_TYPE_KCS.*/
	uint32_t driver_type;
	/* Flag informs the library if in-band driver information should be
	 * probed or not.*/
	uint32_t disable_auto_probe;
	/* Use this specified driver address instead of a probed one.*/
	uint32_t driver_address;
	/* Return sensor names with appropriate entity
	 * id and instance prefixed when appropriate.*/
	bool entity_sensor_names;
	/* frequency for ipmi call*/
	uint32_t freq;
	/* Do not read sensors that cannot be interpreted.*/
	bool ignore_non_interpretable_sensors;
	/* Ignore the scanning bit and read sensors no matter
	 * what.  This option works around motherboards
	 * that incorrectly indicate sensors as disabled.*/
	bool ignore_scanning_disabled;
	/* Attempt to interpret OEM data if read.*/
	bool interpret_oem_data;
	/* BMC Key for 2-key authentication.  Pass NULL ptr to use the
	 * default.  Standard default is the null (e.g. empty) k_g,
	 * which will use the password as the BMC key.  The k_g key need not
	 * be an ascii string.*/
	unsigned char *k_g;
	/* Length of k_g.  Necessary b/c k_g may contain null values in its
	 * key.  Maximum length of 20 bytes.*/
	uint32_t k_g_len;
	/* BMC password. Pass NULL ptr for default password.  Standard
	 * default is the null (e.g. empty) password.  Maximum length of 20
	 * bytes.*/
	char *password;
	/* privilege level to authenticate with.
	 * Supported privilege levels:
	 *   0 = IPMICONSOLE_PRIVILEGE_USER
	 *   1 = IPMICONSOLE_PRIVILEGE_OPERATOR
	 *   2 = IPMICONSOLE_PRIVILEGE_ADMIN
	 * Pass < 0 for default of IPMICONSOLE_PRIVILEGE_ADMIN.*/
	uint32_t privilege_level;
	/* Options for Slurm IPMI plugin*/
	/* sensor num (only for power) */
	uint32_t power_sensor_num;
	char *power_sensors;
	/* Out-of-band Communication Configuration */
	/* Indicate the IPMI protocol version to use
	 * IPMI_MONITORING_PROTOCOL_VERSION_1_5 = 0x00,
	 * IPMI_MONITORING_PROTOCOL_VERSION_2_0 = 0x01,
	 * Pass < 0 for default of IPMI_MONITORING_VERSION_1_5.*/
	uint32_t protocol_version;
	/* Use this register space instead of the probed one.*/
	uint32_t register_spacing;
	/* Re-read the SDR cache*/
	bool reread_sdr_cache;
	/* Specifies the packet retransmission timeout length in
	 * milliseconds.  Pass <= 0 to default 500 (0.5 seconds).*/
	uint32_t retransmission_timeout;
	/* Specifies the session timeout length in milliseconds.  Pass <= 0
	 * to default 60000 (60 seconds).*/
	uint32_t session_timeout;
	/* Iterate through shared sensors if found*/
	bool shared_sensors;
	/* Timeout for the ipmi thread*/
	uint32_t timeout;
	/* BMC username. Pass NULL ptr for default username.  Standard
	 * default is the null (e.g. empty) username.  Maximum length of 16
	 * bytes.*/
	char *username;
	/* Bitwise OR of flags indicating IPMI implementation changes.  Some
	 * BMCs which are non-compliant and may require a workaround flag
	 * for correct operation. Pass IPMICONSOLE_WORKAROUND_DEFAULT for
	 * default.  Standard default is 0, no modifications to the IPMI
	 * protocol.*/
	uint32_t workaround_flags;
	uint32_t variable;
} slurm_ipmi_conf_t;

extern void reset_slurm_ipmi_conf(slurm_ipmi_conf_t *slurm_ipmi_conf);

#endif

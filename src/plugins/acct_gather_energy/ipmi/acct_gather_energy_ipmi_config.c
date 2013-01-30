/*****************************************************************************\
 *  acct_gather_energy_ipmi_config.c - functions for reading ipmi.conf
 *****************************************************************************
 *  Copyright (C) 2012
 *  Written by Bull- Thomas Cadeau
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "acct_gather_energy_ipmi_config.h"

/* Local functions */
static void _clear_slurm_ipmi_conf(slurm_ipmi_conf_t *slurm_ipmi_conf);
extern void free_slurm_ipmi_conf(slurm_ipmi_conf_t *slurm_ipmi_conf)
{
	_clear_slurm_ipmi_conf(slurm_ipmi_conf);
}

static void _clear_slurm_ipmi_conf(slurm_ipmi_conf_t *slurm_ipmi_conf)
{
	if (slurm_ipmi_conf) {
		slurm_ipmi_conf->power_sensor_num = -1;
		slurm_ipmi_conf->freq = -1;
		slurm_ipmi_conf->adjustment = false;
		slurm_ipmi_conf->driver_type = -1;
		slurm_ipmi_conf->disable_auto_probe = 0;
		slurm_ipmi_conf->driver_address = 0;
		slurm_ipmi_conf->register_spacing = 0;
		xfree(slurm_ipmi_conf->driver_device);
		slurm_ipmi_conf->protocol_version = -1;
		xfree(slurm_ipmi_conf->username);
		xfree(slurm_ipmi_conf->password);
		xfree(slurm_ipmi_conf->k_g);
		slurm_ipmi_conf->k_g_len = 0;
		slurm_ipmi_conf->privilege_level = -1;
		slurm_ipmi_conf->authentication_type = -1;
		slurm_ipmi_conf->cipher_suite_id = 0;
		slurm_ipmi_conf->session_timeout = 0;
		slurm_ipmi_conf->retransmission_timeout = 0;
		slurm_ipmi_conf->workaround_flags = 0;
		slurm_ipmi_conf->reread_sdr_cache = false;
		slurm_ipmi_conf->ignore_non_interpretable_sensors = true;
		slurm_ipmi_conf->bridge_sensors = false;
		slurm_ipmi_conf->interpret_oem_data = false;
		slurm_ipmi_conf->shared_sensors = false;
		slurm_ipmi_conf->discrete_reading = false;
		slurm_ipmi_conf->ignore_scanning_disabled = false;
		slurm_ipmi_conf->assume_bmc_owner = false;
		slurm_ipmi_conf->entity_sensor_names = false;
	}
}


/*
 * read_slurm_ipmi_conf - load the Slurm ipmi configuration from the
 *	ipmi.conf file.
 * RET SLURM_SUCCESS if no error, otherwise an error code
 */
extern int read_slurm_ipmi_conf(slurm_ipmi_conf_t *slurm_ipmi_conf)
{
	s_p_options_t options[] = {
		{"DriverType", S_P_UINT32},
		{"DisableAutoProbe", S_P_UINT32},
		{"DriverAddress", S_P_UINT32},
		{"RegisterSpacing", S_P_UINT32},
		{"DriverDevice", S_P_STRING},
		{"ProtocolVersion", S_P_UINT32},
		{"Username", S_P_STRING},
		{"Password", S_P_STRING},
//		{"k_g", S_P_STRING},
//		{"k_g_len", S_P_UINT32},
		{"PrivilegeLevel", S_P_UINT32},
		{"AuthenticationType", S_P_UINT32},
		{"CipherSuiteId", S_P_UINT32},
		{"SessionTimeout", S_P_UINT32},
		{"RetransmissionTimeout", S_P_UINT32},
		{"WorkaroundFlags", S_P_UINT32},
		{"RereadSdrCache", S_P_BOOLEAN},
		{"IgnoreNonInterpretableSensors", S_P_BOOLEAN},
		{"BridgeSensors", S_P_BOOLEAN},
		{"InterpretOemData", S_P_BOOLEAN},
		{"SharedSensors", S_P_BOOLEAN},
		{"DiscreteReading", S_P_BOOLEAN},
		{"IgnoreScanningDisabled", S_P_BOOLEAN},
		{"AssumeBmcOwner", S_P_BOOLEAN},
		{"EntitySensorNames", S_P_BOOLEAN},
		{"Frequency", S_P_UINT32},
		{"CalcAdjustment", S_P_BOOLEAN},
		{"PowerSensor", S_P_UINT32},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	struct stat buf;
	uint32_t tmp;

	/* Set initial values */
	xassert(slurm_ipmi_conf);


	/* Get the ipmi.conf path and validate the file */
	conf_path = get_extra_conf_path("ipmi.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		info("No ipmi.conf file (%s)", conf_path);
	} else {
		debug("Reading ipmi.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false) ==
		    SLURM_ERROR) {
			fatal("Could not open/read/parse ipmi.conf file %s",
			      conf_path);
		}

		/* ipmi initialisation parameters */
		s_p_get_uint32(&slurm_ipmi_conf->driver_type,
			       "DriverType", tbl);
		s_p_get_uint32(&slurm_ipmi_conf->disable_auto_probe,
			       "DisableAutoProbe", tbl);
		s_p_get_uint32(&slurm_ipmi_conf->driver_address,
			       "DriverAddress", tbl);
		s_p_get_uint32(&slurm_ipmi_conf->register_spacing,
			       "RegisterSpacing", tbl);

		s_p_get_string(&slurm_ipmi_conf->driver_device,
			       "DriverDevice", tbl);

		s_p_get_uint32(&slurm_ipmi_conf->protocol_version,
			       "ProtocolVersion", tbl);

		if (!s_p_get_string(&slurm_ipmi_conf->username,
				    "Username", tbl))
			slurm_ipmi_conf->username = xstrdup("foousername");

		s_p_get_string(&slurm_ipmi_conf->password,
			       "Password", tbl);
		if (! slurm_ipmi_conf->password)
			slurm_ipmi_conf->password =
				xstrdup("foopassword");

		s_p_get_uint32(&slurm_ipmi_conf->privilege_level,
			       "PrivilegeLevel", tbl);
		s_p_get_uint32(&slurm_ipmi_conf->authentication_type,
			       "AuthenticationType", tbl);
		s_p_get_uint32(&slurm_ipmi_conf->cipher_suite_id,
			       "CipherSuiteId", tbl);
		s_p_get_uint32(&slurm_ipmi_conf->session_timeout,
			       "SessionTimeout", tbl);
		s_p_get_uint32(&slurm_ipmi_conf->retransmission_timeout,
			       "RetransmissionTimeout", tbl);
		s_p_get_uint32(&slurm_ipmi_conf-> workaround_flags,
			       "WorkaroundFlags", tbl);

		if (!s_p_get_boolean(&slurm_ipmi_conf->reread_sdr_cache,
				     "RereadSdrCache", tbl))
			slurm_ipmi_conf->reread_sdr_cache = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->
				     ignore_non_interpretable_sensors,
				     "IgnoreNonInterpretableSensors", tbl))
			slurm_ipmi_conf->ignore_non_interpretable_sensors =
				false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->bridge_sensors,
				     "BridgeSensors", tbl))
			slurm_ipmi_conf->bridge_sensors = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->interpret_oem_data,
				     "InterpretOemData", tbl))
			slurm_ipmi_conf->interpret_oem_data = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->shared_sensors,
				     "SharedSensors", tbl))
			slurm_ipmi_conf->shared_sensors = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->discrete_reading,
				     "DiscreteReading", tbl))
			slurm_ipmi_conf->discrete_reading = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->ignore_scanning_disabled,
				     "IgnoreScanningDisabled", tbl))
			slurm_ipmi_conf->ignore_scanning_disabled = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->assume_bmc_owner,
				     "AssumeBmcOwner", tbl))
			slurm_ipmi_conf->assume_bmc_owner = false;
		if (!s_p_get_boolean(&slurm_ipmi_conf->entity_sensor_names,
				     "EntitySensorNames", tbl))
			slurm_ipmi_conf->entity_sensor_names = false;

		s_p_get_uint32 (&tmp,
				"Frequency", tbl);
		slurm_ipmi_conf->freq = (time_t) tmp;

		if (!s_p_get_boolean(&(slurm_ipmi_conf->adjustment),
				"CalcAdjustment", tbl))
			slurm_ipmi_conf->adjustment = false;

		s_p_get_uint32 (&tmp,
				"PowerSensor", tbl);
		slurm_ipmi_conf->power_sensor_num = (uint32_t) tmp;

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return SLURM_SUCCESS;
}

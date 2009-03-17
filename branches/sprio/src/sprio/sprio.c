/*****************************************************************************\
 *  sprio.c - Display the priority components of jobs in the slurm system
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Don Lipari <lipari1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDINT_H
#  include <stdint.h>
#endif

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#ifdef HAVE_TERMCAP_H
#  include <termcap.h>
#endif

#include <sys/ioctl.h>
#include <termios.h>

#include "src/common/slurm_priority.h"
#include "src/common/xstring.h"
#include "src/sprio/sprio.h"


/********************
 * Global Variables *
 ********************/
struct sprio_parameters params;
uint32_t weight_age; /* weight for age factor */
uint32_t weight_fs; /* weight for Fairshare factor */
uint32_t weight_js; /* weight for Job Size factor */
uint32_t weight_part; /* weight for Partition factor */
uint32_t weight_qos; /* weight for QOS factor */

static int _get_info(priority_factors_request_msg_t *factors_req,
		     priority_factors_response_msg_t **factors_resp);

int main (int argc, char *argv[])
{
	char *temp = NULL;
	int error_code = SLURM_SUCCESS;
	priority_factors_request_msg_t req_msg;
	priority_factors_response_msg_t *resp_msg = NULL;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	/* Check to see if we are running a supported accounting plugin */
	temp = slurm_get_priority_type();
	if(strcasecmp(temp, "priority/multifactor")) {
		fprintf (stderr, "You are not running a supported "
			 "priority plugin\n(%s).\n"
			 "Only 'priority/multifactor' is supported.\n",
			temp);
		xfree(temp);
		exit(1);
	}
	xfree(temp);

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_USER, NULL);

	weight_age  = slurm_get_priority_weight_age();
	weight_fs   = slurm_get_priority_weight_fairshare();
	weight_js   = slurm_get_priority_weight_job_size();
	weight_part = slurm_get_priority_weight_partition();
	weight_qos  = slurm_get_priority_weight_qos();

	parse_command_line( argc, argv );
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}

	memset(&req_msg, 0, sizeof(priority_factors_request_msg_t));

	if (params.jobs)
		req_msg.job_id_list = params.job_list;
	else
		req_msg.job_id_list = NULL;

	if (params.users)
		req_msg.uid_list = params.user_list;
	else
		req_msg.uid_list = NULL;

	error_code = _get_info(&req_msg, &resp_msg);

	if (params.format == NULL) {
		if (params.normalized) {
			if (params.long_list)
				params.format = "%.7i %.8u %10y %10a %10f %10j %10p %10q";
			else{
				params.format = xstrdup("%.7i");
				if (params.users)
					xstrcat(params.format, " %.8u");
				xstrcat(params.format, " %10y");
				if (weight_age)
					xstrcat(params.format, " %10a");
				if (weight_fs)
					xstrcat(params.format, " %10f");
				if (weight_js)
					xstrcat(params.format, " %10j");
				if (weight_part)
					xstrcat(params.format, " %10p");
				if (weight_qos)
					xstrcat(params.format, " %10q");
			}
		} else {
			if (params.long_list)
				params.format = "%.7i %.8u %.10Y %.10A %.10F %.10J %.10P %.10Q %.6N";
			else{
				params.format = xstrdup("%.7i");
				if (params.users)
					xstrcat(params.format, " %.8u");
				xstrcat(params.format, " %.10Y");
				if (weight_age)
					xstrcat(params.format, " %.10A");
				if (weight_fs)
					xstrcat(params.format, " %.10F");
				if (weight_js)
					xstrcat(params.format, " %.10J");
				if (weight_part)
					xstrcat(params.format, " %.10P");
				if (weight_qos)
					xstrcat(params.format, " %.10Q");
			}
		}
	}

	/* create the format list from the format */
	parse_format(params.format);

	print_jobs_array(resp_msg->priority_factors_list, params.format_list);

#if 0
	/* Free storage here if we want to verify that logic.
	 * Since we exit next, this is not important */
 	list_destroy(params.format_list);
	slurm_free_priority_factors_response_msg(resp_msg);
#endif

	exit (error_code);
}

static int _get_info(priority_factors_request_msg_t *factors_req,
		     priority_factors_response_msg_t **factors_resp)
{
	int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

        req_msg.msg_type = REQUEST_PRIORITY_FACTORS;
        req_msg.data     = factors_req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_PRIORITY_FACTORS:
		*factors_resp =
			(priority_factors_response_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*factors_resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

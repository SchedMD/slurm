
/*****************************************************************************\
 *  jobacct_gold.c - NO-OP slurm job completion logging plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>.
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *
 *  This file is patterned after jobcomp_gold.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <netdb.h>

#include <stdio.h>
#include <slurm/slurm_errno.h>

#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"

#define SOCKET_ERROR -1
#define MAX_SHUTDOWN_RETRY 5

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Job accounting GOLD plugin";
const char plugin_type[] = "jobacct/gold";
const uint32_t plugin_version = 100;

static slurm_fd gold_fd; // gold connection 
static char *gold_keyfile = NULL;
static char *gold_host = NULL;
static uint16_t gold_port = 0;
static char basis_64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int _is_base64(unsigned char c) {
	if((c >= '/' && c <= '9') 
	   || (c >= 'A' && c <= 'Z')
	   || (c >= 'a' && c <= 'z')
	   || (c == '+')) 
		return 1;
	return 0;
}

/* must free with xfree after use */
char *_encode_base64(unsigned char const* in_str, unsigned int in_len)
{
	char *ret = NULL;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];
	int pos = 0;
	/* calculate the length of the result */
	int rlen = (in_len+2) / 3 * 4;	 /* encoded bytes */

	rlen++; /* for the eol */
	ret = xmalloc(rlen);
	
	debug2("encoding %s", in_str);

	while (in_len--) {
		char_array_3[i++] = *(in_str++);
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4)
				+ ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2)
				+ ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;
			
			for(i = 0; (i <4) ; i++)
				ret[pos++] = basis_64[char_array_4[i]];
			i = 0;
		}
	}
	
	if (i) {
		for(j = i; j < 3; j++)
			char_array_3[j] = '\0';
		
		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4)
			+ ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2)
			+ ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;
		
		for (j = 0; (j < i + 1); j++)
			ret[pos++] = basis_64[char_array_4[j]];
		
		while((i++ < 3))
			ret[pos++] = '=';
		
	}

	debug2("encoded %s", ret);
	
	return ret;
}

/* must free with xfree after use */
char *_decode_base64(const char *in_str)
{
	int pos = 0;
	int in_len = strlen(in_str);
	int i = 0;
	int j = 0;
	int in_pos = 0;
	unsigned char char_array_4[4], char_array_3[3];
	char *ret = NULL;

	int rlen = in_len * 3 / 4; /* always enough, but sometimes too
				    * much */
       	
	debug2("decoding %s", in_str);

	ret = xmalloc(sizeof(char)*rlen);
	memset(ret, 0, rlen);
	
	while (in_len-- && ( in_str[in_pos] != '=')
	       && _is_base64(in_str[in_pos])) {
		char_array_4[i++] = in_str[in_pos];
		in_pos++;
		if (i == 4) {
			for (i=0; i<4; i++) {
				int found = 0;
				while(basis_64[found] 
				      && basis_64[found] != char_array_4[i])
					found++;
				if(!basis_64[found]) 
					found = 0;
				char_array_4[i] = found;
			}
			char_array_3[0] = (char_array_4[0] << 2) 
				+ ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) 
				+ ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6)
				+ char_array_4[3];
			for (i = 0; i<3; i++)
				ret[pos++] = char_array_3[i];
			i = 0;
		}
	}

	if (i) {
		for (j=i; j<4; j++)
			char_array_4[j] = 0;

		for (j=0; j<4; j++) {
			int found = 0;
			while(basis_64[found] 
			      && basis_64[found] != char_array_4[j])
				found++;
			if(!basis_64[found]) 
				found = 0;
			
			char_array_4[j] = found;
		}

		char_array_3[0] = (char_array_4[0] << 2) 
			+ ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4)
			+ ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) 
			+ char_array_4[3];

		for (j = 0; (j < i - 1); j++)
			ret[pos++] = char_array_3[j];
	}

	debug2("decoded %s", ret);

	return ret;
}

int _start_gold_communication()
{
	static slurm_addr gold_addr;
	static int gold_addr_set = 0;
	char *header = "POST /SSSRMAP3 HTTP/1.1\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nTransfer-Encoding: chunked\r\n\r\n";
	int rc = 0;
	
	if(!gold_addr_set) {
		slurm_set_addr(&gold_addr, gold_port, gold_host);
		gold_addr_set = 1;
	}
	
	if ((gold_fd = slurm_open_msg_conn(&gold_addr)) < 0) {
		error("_start_communication to %s: %m", gold_host);
		return SLURM_ERROR;
	}

	info("sending header \"%s\" %d", header, strlen(header));
	rc = _slurm_send_timeout(gold_fd, header, strlen(header),
				 SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				 (slurm_get_msg_timeout() * 1000));
	
	if (rc < 0) 
		error("slurm_msg_sendto: %m");
	

	return SLURM_SUCCESS;

}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * The following routines are called by slurmctld
 */

/*
 * The following routines are called by slurmd
 */
int jobacct_p_init_struct(struct jobacctinfo *jobacct, 
			  jobacct_id_t *jobacct_id)
{
	return SLURM_SUCCESS;
}

struct jobacctinfo *jobacct_p_alloc(jobacct_id_t *jobacct_id)
{
	return NULL;
}

void jobacct_p_free(struct jobacctinfo *jobacct)
{
	return;
}

int jobacct_p_setinfo(struct jobacctinfo *jobacct, 
		      enum jobacct_data_type type, void *data)
{
	return SLURM_SUCCESS;
	
}

int jobacct_p_getinfo(struct jobacctinfo *jobacct, 
		      enum jobacct_data_type type, void *data)
{
	return SLURM_SUCCESS;
}

void jobacct_p_aggregate(struct jobacctinfo *dest, struct jobacctinfo *from)
{
	return;
}

void jobacct_p_2_sacct(sacct_t *sacct, struct jobacctinfo *jobacct)
{
	return;
}

void jobacct_p_pack(struct jobacctinfo *jobacct, Buf buffer)
{
	return;
}

int jobacct_p_unpack(struct jobacctinfo **jobacct, Buf buffer)
{
	return SLURM_SUCCESS;
}


int jobacct_p_init_slurmctld(char *gold_info)
{
	char *total = "/etc/gold/auth_key:localhost:7112";
	int found = 0;
	int i=0, j=0;

	debug2("jobacct_init() called");
	if(gold_info) 
		total = gold_info;
	i = 0;
	while(total[j]) {
		if(total[j] == ':') {
			switch(found) {
			case 0: // keyfile name
				gold_keyfile = xstrndup(total+i, j-i);
				break;
			case 1: // host name
				gold_host = xstrndup(total+i, j-i);
				break;
			case 2: // port
				gold_port = atoi(total+i);
				break;
			}
			found++;
			i = j+1;	
		}
		j++;
	}
	if(!gold_port) 
		gold_port = atoi(total+i);

	if (!gold_keyfile || *gold_keyfile != '/')
		fatal("JobAcctLogfile should be in the format of "
		      "gold_auth_key_file_path:goldd_host:goldd_port "
		      "bad key file");
	if(!gold_host)
		fatal("JobAcctLogfile should be in the format of "
		      "gold_auth_key_file_path:goldd_host:goldd_port "
		      "bad host");
	if(!gold_port) 
		fatal("JobAcctLogfile should be in the format of "
		      "gold_auth_key_file_path:goldd_host:goldd_port "
		      "bad port");
	
	debug2("got %s %s %d", gold_keyfile, gold_host, gold_port);
	_start_gold_communication();
	return SLURM_SUCCESS;
}

int jobacct_p_fini_slurmctld()
{
	int retry = 0;
	
	/* 
	 *  Attempt to close an open connection
	 */
	while ((slurm_shutdown_msg_conn(gold_fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY) {
			break;
		}
	}

	xfree(gold_keyfile);
	xfree(gold_host);
	return SLURM_SUCCESS;
}

int jobacct_p_job_start_slurmctld(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

int jobacct_p_job_complete_slurmctld(struct job_record *job_ptr) 
{
	return  SLURM_SUCCESS;
}

int jobacct_p_step_start_slurmctld(struct step_record *step)
{
	return SLURM_SUCCESS;	
}

int jobacct_p_step_complete_slurmctld(struct step_record *step)
{
	return SLURM_SUCCESS;	
}

int jobacct_p_suspend_slurmctld(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

int jobacct_p_startpoll(int frequency)
{
	info("jobacct GOLD plugin loaded");
	debug3("slurmd_jobacct_init() called");
	
	return SLURM_SUCCESS;
}

int jobacct_p_endpoll()
{
	return SLURM_SUCCESS;
}

int jobacct_p_set_proctrack_container_id(uint32_t id)
{
	return SLURM_SUCCESS;
}

int jobacct_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	return SLURM_SUCCESS;
}

struct jobacctinfo *jobacct_p_stat_task(pid_t pid)
{
	return NULL;
}

struct jobacctinfo *jobacct_p_remove_task(pid_t pid)
{
	return NULL;
}

void jobacct_p_suspend_poll()
{
	return;
}

void jobacct_p_resume_poll()
{
	return;
}

#ifndef _SLURM_PROTOCOL_UTIL_H
#define _SLURM_PROTOCOL_UTIL_H

#if HAVE_CONFIG_H
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <stdio.h>

#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/slurm_protocol_common.h>

/* slurm protocol header functions */
uint32_t check_header_version(header_t * header);
void init_header(header_t * header, slurm_msg_type_t msg_type,
		 uint16_t flags);

/* io stream header functions */
uint32_t check_io_stream_header_version(slurm_io_stream_header_t * header);
void init_io_stream_header(slurm_io_stream_header_t * header, char *key,
			   uint32_t task_id, uint16_t type);
void update_header(header_t * header, uint32_t cred_length,
		   uint32_t msg_length);

/* debug print methods */
void slurm_print_job_credential(FILE * stream,
				slurm_job_credential_t * credential);
void slurm_print_launch_task_msg(launch_tasks_request_msg_t * msg);

#endif /* !_SLURM_PROTOCOL_UTIL_H */

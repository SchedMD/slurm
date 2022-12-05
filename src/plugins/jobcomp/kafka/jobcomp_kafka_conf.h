/*****************************************************************************\
 *  jobcomp_kafka_config.h - Parse config helper header for jobcomp/kafka.
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Alejandro Sanchez <alex@schedmd.com>
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

#ifndef _HAVE_JOBCOMP_KAFKA_CONFIG_H
#define _HAVE_JOBCOMP_KAFKA_CONFIG_H

/* Purge in-flight broker messages. Experimental, undocumented. */
#define KAFKA_CONF_FLAG_PURGE_IN_FLIGHT			SLURM_BIT(0)

/* Non-blocking purge. Experimental, undocumented. */
#define KAFKA_CONF_FLAG_PURGE_NON_BLOCKING		SLURM_BIT(1)

/* Attempt requeue message on RD_KAFKA_RESP_ERR__MSG_TIMED_OUT. */
#define KAFKA_CONF_FLAG_REQUEUE_ON_MSG_TIMEOUT		SLURM_BIT(2)

/* Attempt requeue message on purge in-flight. Experimental, undocumented. */
#define KAFKA_CONF_FLAG_REQUEUE_PURGE_IN_FLIGHT		SLURM_BIT(3)

typedef struct {
	uint32_t flags;			/* Configuration flags. */
	int flush_timeout;		/* rd_kafka_flush() timeout in ms. */
	uint32_t poll_interval;		/* Sec. between rd_kafka_poll(). */
	char *topic;			/* Target topic name. */
} kafka_conf_t;

extern kafka_conf_t *kafka_conf;
extern pthread_rwlock_t kafka_conf_rwlock;
extern list_t *rd_kafka_conf_list;

extern void jobcomp_kafka_conf_init(void);
extern void jobcomp_kafka_conf_fini(void);

/*
 * Open a file and parse key=value options, skipping blanks and comments.
 * Add parsed key=value options as config_key_pair_t's to rd_kafka_conf_list.
 *
 * Since librdkafka parameters can change with time, we don't want to maintain
 * the list of predefined options in sync with the library. That's why this just
 * parses options without expecting anything specific, just a key-valued file.
 *
 * NOTE: This function could be eligible to be made globally available somewhere
 * under src/common, potentially src/common/parse_config, passing the list as
 * argument. But for now it feels less sophisticated than s_p_parse_file().
 *
 * IN: char *location
 * RET: SLURM_ERROR if problem opening the file, SLURM_SUCCESS otherwise.
 */
extern int jobcomp_kafka_conf_parse_location(char *location);

extern void jobcomp_kafka_conf_parse_params(void);

#endif

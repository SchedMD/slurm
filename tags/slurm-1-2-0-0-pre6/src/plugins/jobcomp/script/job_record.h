/*****************************************************************************\
 *  job_record.h - Header for job_record structure.
 *****************************************************************************
 *  Produced at Center for High Performance Computing, North Dakota State 
 *  University
 *  Written by Nathan Huff <nhuff@geekshanty.com>
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
\*****************************************************************************/


#ifndef _JOB_RECORD_H
#define _JOB_RECORD_H

/* Structure to contain job completion information */
struct job_record_ {
    uint32_t job_id;
    uint32_t user_id;
    char *job_name;
    char *job_state;
    char *partition;
    uint32_t limit;
    uint16_t batch_flag;
    time_t submit;
    time_t start;
    time_t end;
    char *node_list;
};

typedef struct job_record_ * job_record;

/* Create a job record */
job_record job_record_create(uint32_t job_id, uint32_t user_id, char *job_name,
    char *job_state, char *partition, uint32_t limit, time_t start, time_t end,
    time_t submit, uint16_t batch_flag, char *node_list);

/* Destroy a job record */
void job_record_destroy(void *j);

#endif /* _JOB_RECORD_H */ 

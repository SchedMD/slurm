/*****************************************************************************\
 *  part_data.h - Functions for structures dealing with partitions unique
 *                to the select plugin.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Derived in large part from select/cons_[res|tres] plugins
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

#ifndef _CONS_COMMON_PART_DATA_H
#define _CONS_COMMON_PART_DATA_H

/* a partition's per-row core allocation bitmap arrays (1 bitmap per node) */
struct part_row_data {
	struct job_resources **job_list;/* List of jobs in this row */
	uint32_t job_list_size;		/* Size of job_list array */
	uint32_t num_jobs;		/* Number of occupied entries in
					 * job_list array */
	bitstr_t **row_bitmap;		/* contains core bitmap for all jobs in
					 * this row, one bitstr_t for each node
					 * In cons_res only the first ptr is
					 * used.
					 */
};

/* partition core allocation bitmap arrays (1 bitmap per node) */
struct part_res_record {
	struct part_res_record *next; /* Ptr to next part_res_record */
	uint16_t num_rows;	      /* Number of elements in "row" array */
	struct part_record *part_ptr; /* controller part record pointer */
	struct part_row_data *row;    /* array of rows containing jobs */
};

extern struct part_res_record *select_part_record;

/*
 * Add job resource use to the partition data structure
 */
extern void part_data_add_job_to_row(struct job_resources *job,
				     struct part_row_data *r_ptr);

/*
 * part_data_build_row_bitmaps: A job has been removed from the given partition,
 *                     so the row_bitmap(s) need to be reconstructed.
 *                     Optimize the jobs into the least number of rows,
 *                     and make the lower rows as dense as possible.
 *
 * IN p_ptr - the partition that has jobs to be optimized
 * IN job_ptr - pointer to single job removed, pass NULL to completely rebuild
 */
extern void part_data_build_row_bitmaps(struct part_res_record *p_ptr,
					struct job_record *job_ptr);

/* (re)create the global select_part_record array */
extern void part_data_create_array(void);

/* Delete the given list of partition data */
extern void part_data_destroy_res(struct part_res_record *this_ptr);

/* Delete the given partition row data */
extern void part_data_destroy_row(struct part_row_data *row, uint16_t num_rows);

/* Log contents of partition structure */
extern void part_data_dump_res(struct part_res_record *p_ptr);

/* Create a duplicate part_res_record list */
extern struct part_res_record *part_data_dup_res(
	struct part_res_record *orig_ptr);

/* sort the rows of a partition from "most allocated" to "least allocated" */
extern void part_data_sort_res(struct part_res_record *p_ptr);

/* Create a duplicate part_row_data struct */
extern struct part_row_data *part_data_dup_row(struct part_row_data *orig_row,
					       uint16_t num_rows);


#endif /* _CONS_COMMON_PART_DATA_H */

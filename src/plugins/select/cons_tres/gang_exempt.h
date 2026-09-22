/*****************************************************************************\
 *  gang_exempt.h - Track cores exempt from GANG oversubscription
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _CONS_TRES_GANG_EXEMPT_H
#define _CONS_TRES_GANG_EXEMPT_H

/*
 * Return the cores no job may be oversubscribed onto under gang scheduling,
 * bringing them up to date first. Reading them through here is what keeps them
 * current, so the array itself is private to the module.
 * RET the exempt cores, or NULL if nothing is exempt
 */
extern bitstr_t **gang_exempt_get_cores(void);

/*
 * Offer job_ptr's cores as exempt from sharing. Needs no locks: whether the
 * job really is exempt is settled by the next read of the exempt cores, which
 * happens before anything can be placed on them.
 * IN job_ptr - the job that now holds an allocation
 */
extern void gang_exempt_add_job(job_record_t *job_ptr);

/*
 * Stop offering job_ptr. A no-op for a job that was never offered.
 * IN job_ptr - the job that no longer holds an allocation
 */
extern void gang_exempt_remove_job(job_record_t *job_ptr);

/* Mark the exempt cores stale, so the next read of them re-derives them. */
extern void gang_exempt_mark_stale(void);

/*
 * Discard the exempt cores after the node count may have changed, rather than
 * clearing an array sized to the old count.
 */
extern void gang_exempt_node_init(void);

/* Release everything the exempt set holds. */
extern void gang_exempt_fini(void);

#endif /* !_CONS_TRES_GANG_EXEMPT_H */

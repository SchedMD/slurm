/*****************************************************************************
 *  slurm_config.h - slurm.conf reader
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
 *  UCRL-CODE-217948.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _SLURM_CONFIG_H
#define _SLURM_CONFIG_H

#include "src/common/parse_config.h"

int parse_nodename(void **dest, slurm_parser_enum_t type,
		   const char *key, const char *value, const char *leftover);
void destroy_nodename(void *ptr);
int parse_partitionname(void **dest, slurm_parser_enum_t type,
		   const char *key, const char *value, const char *leftover);
void destroy_partitionname(void *ptr);

s_p_options_t slurm_conf_options[] = {
	{"AuthType", S_P_STRING},
	{"CheckpointType", S_P_STRING},
	{"CacheGroups", S_P_LONG},
	{"BackupAddr", S_P_STRING},
	{"BackupController", S_P_STRING},
	{"ControlAddr", S_P_STRING},
	{"ControlMachine", S_P_STRING},
	{"Epilog", S_P_STRING},
	{"FastSchedule", S_P_LONG},
	{"FirstJobId", S_P_LONG},
	{"HashBase", S_P_LONG}, /* defunct */
	{"HeartbeatInterval", S_P_LONG},
	{"InactiveLimit", S_P_LONG},
	{"JobAcctloc", S_P_STRING},
	{"JobAcctParameters", S_P_STRING},
	{"JobAcctType", S_P_STRING},
	{"JobCompLoc", S_P_STRING},
	{"JobCompType", S_P_STRING},
	{"JobCredentialPrivateKey", S_P_STRING},
	{"JobCredentialPublicCertificate", S_P_STRING},
	{"KillTree", S_P_LONG}, /* FIXME - defunct? */
	{"KillWait", S_P_LONG},
	{"MaxJobCount", S_P_LONG},
	{"MinJobAge", S_P_LONG},
	{"MpichGmDirectSupport", S_P_LONG},
	{"MpiDefault", S_P_STRING},
	{"NodeName", S_P_ARRAY,
	 parse_nodename, destroy_nodename},
	{"PartitionName", S_P_ARRAY,
	 parse_partitionname, destroy_partitionname},
	{"PluginDir", S_P_STRING},
	{"ProctrackType", S_P_STRING},
	{"Prolog", S_P_STRING},
	{"PropagateResourceLimitsExcept", S_P_STRING},
	{"PropagateResourceLimits", S_P_STRING},
	{"ReturnToService", S_P_LONG},
	{"SchedulerAuth", S_P_STRING},
	{"SchedulerPort", S_P_LONG},
	{"SchedulerRootFilter", S_P_LONG},
	{"SchedulerType", S_P_STRING},
	{"SelectType", S_P_STRING},
	{"SlurmUser", S_P_STRING},
	{"SlurmctldDebug", S_P_LONG},
	{"SlurmctldLogFile", S_P_STRING},
	{"SlurmctldPidFile", S_P_STRING},
	{"SlurmctldPort", S_P_LONG},
	{"SlurmctldTimeout", S_P_LONG},
	{"SlurmdDebug", S_P_LONG},
	{"SlurmdLogFile", S_P_STRING},
	{"SlurmdPidFile",  S_P_STRING},
	{"SlurmdPort", S_P_LONG},
	{"SlurmdSpoolDir", S_P_STRING},
	{"SlurmdTimeout", S_P_LONG},
	{"SrunEpilog", S_P_STRING},
	{"SrunProlog", S_P_STRING},
	{"StateSaveLocation", S_P_STRING},
	{"SwitchType", S_P_STRING},
	{"TaskEpilog", S_P_STRING},
	{"TaskProlog", S_P_STRING},
	{"TaskPlugin", S_P_STRING},
	{"TmpFS", S_P_STRING},
	{"TreeWidth", S_P_LONG},
	{"WaitTime", S_P_LONG},
	{NULL}
};

s_p_options_t slurm_nodename_options[] = {
	{"NodeName", S_P_STRING},
	{"NodeHostname", S_P_STRING},
	{"NodeAddr", S_P_STRING},
	{"Feature", S_P_STRING},
	{"Port", S_P_LONG},
	{"Procs", S_P_LONG},
	{"RealMemory", S_P_LONG},
	{"Reason", S_P_STRING},
	{"State", S_P_STRING},
	{"TmpDisk", S_P_LONG},
	{"Weight", S_P_LONG},
	{NULL}
};

s_p_options_t slurm_partition_options[] = {
	{"PartitionName", S_P_STRING},
	{"AllowGroups", S_P_STRING},
	{"Default", S_P_STRING},
	{"Hidden", S_P_STRING},
	{"RootOnly", S_P_STRING},
	{"MaxTime", S_P_STRING},
	{"MaxNodes", S_P_LONG},
	{"MinNodes", S_P_LONG},
	{"Nodes", S_P_STRING},
	{"Shared", S_P_STRING},
	{"State", S_P_STRING},
	{NULL}
};

#endif /* !_SLURM_CONFIG_H */

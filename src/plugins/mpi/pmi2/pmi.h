/*****************************************************************************\
 **  pmi.c - PMI common definitions
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
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

#ifndef _PMI_H
#define _PMI_H

/***********************************************************\
 * PMI1 definitions
\***********************************************************/
/* from src/pmi/simple/simeple_pmiutil.h */
#define PMIU_MAXLINE 1024

/* from src/pm/hydra/pm/pmiserv/pmi_common.h */
#define MAXKEYLEN    64 /* max length of key in keyval space */
#define MAXVALLEN  1024 /* max length of value in keyval space */
#define MAXNAMELEN  256 /* max length of various names */
#define MAXKVSNAME  MAXNAMELEN  /* max length of a kvsname */


#define GETMAXES_CMD             "get_maxes"
#define MAXES_CMD                "maxes"
#define GETUNIVSIZE_CMD          "get_universe_size"
#define UNIVSIZE_CMD             "universe_size"
#define GETAPPNUM_CMD            "get_appnum"
#define APPNUM_CMD               "appnum"
#define BARRIERIN_CMD            "barrier_in"
#define BARRIEROUT_CMD           "barrier_out"
#define FINALIZE_CMD             "finalize"
#define FINALIZEACK_CMD          "finalize_ack"
#define ABORT_CMD                "abort"
#define PUT_CMD                  "put"
#define PUTRESULT_CMD            "put_result"
#define GET_CMD                  "get"
#define GETRESULT_CMD            "get_result"
#define GETBYIDX_CMD             "getbyidx"
#define GETBYIDXRESULTS_CMD      "getbyidx_results"
#define SPAWNRESULT_CMD          "spawn_result"

#define MCMD_KEY           "mcmd"
#define ENDCMD_KEY         "endcmd"
#define KVSNAMEMAX_KEY     "kvsname_max"
#define KEYLENMAX_KEY      "keylen_max"
#define VALLENMAX_KEY      "vallen_max"
#define SIZE_KEY           "size"
#define APPNUM_KEY         "appnum"
#define EXECNAME_KEY       "execname"
#define NPROCS_KEY         "nprocs"
#define ARGCNT_KEY         "argcnt"
#define INFONUM_KEY        "info_num"
#define TOTSPAWNS_KEY      "totspawns"
#define SPAWNSSOFAR_KEY    "spawnssofar"
#define KVSNAME_KEY        "kvsname"
#define PREPUTNUM_KEY      "preput_num"
#define PREPUTKEY_KEY      "preput_key_"
#define PREPUTVAL_KEY      "preput_val_"


/***********************************************************\
 * PMI2 definitions
\***********************************************************/

/* from src/include/pmi2.h of mpich2 */
#define PMI2_SUCCESS                0
#define PMI2_FAIL                   -1
#define PMI2_ERR_INIT               1
#define PMI2_ERR_NOMEM              2
#define PMI2_ERR_INVALID_ARG        3
#define PMI2_ERR_INVALID_KEY        4
#define PMI2_ERR_INVALID_KEY_LENGTH 5
#define PMI2_ERR_INVALID_VAL        6
#define PMI2_ERR_INVALID_VAL_LENGTH 7
#define PMI2_ERR_INVALID_LENGTH     8
#define PMI2_ERR_INVALID_NUM_ARGS   9
#define PMI2_ERR_INVALID_ARGS       10
#define PMI2_ERR_INVALID_NUM_PARSED 11
#define PMI2_ERR_INVALID_KEYVALP    12
#define PMI2_ERR_INVALID_SIZE       13
#define PMI2_ERR_OTHER              14

#define PMI2_MAX_KEYLEN 64
#define PMI2_MAX_VALLEN 1024
#define PMI2_MAX_ATTRVALUE 1024
#define PMI2_ID_NULL -1

/* modified from src/pmi/pmi2/simple2pmi.h of mpich2 */
#define FULLINIT_CMD           "fullinit"
#define FULLINITRESP_CMD       "fullinit-response"
#define FINALIZE_CMD           "finalize"
#define FINALIZERESP_CMD       "finalize-response"
#define ABORT_CMD              "abort"
#define JOBGETID_CMD           "job-getid"
#define JOBGETIDRESP_CMD       "job-getid-response"
#define JOBCONNECT_CMD         "job-connect"
#define JOBCONNECTRESP_CMD     "job-connect-response"
#define JOBDISCONNECT_CMD      "job-disconnect"
#define JOBDISCONNECTRESP_CMD  "job-disconnect-response"
#define KVSPUT_CMD             "kvs-put"
#define KVSPUTRESP_CMD         "kvs-put-response"
#define KVSFENCE_CMD           "kvs-fence"
#define KVSFENCERESP_CMD       "kvs-fence-response"
#define KVSGET_CMD             "kvs-get"
#define KVSGETRESP_CMD         "kvs-get-response"
#define GETNODEATTR_CMD        "info-getnodeattr"
#define GETNODEATTRRESP_CMD    "info-getnodeattr-response"
#define PUTNODEATTR_CMD        "info-putnodeattr"
#define PUTNODEATTRRESP_CMD    "info-putnodeattr-response"
#define GETJOBATTR_CMD         "info-getjobattr"
#define GETJOBATTRRESP_CMD     "info-getjobattr-response"
#define NAMEPUBLISH_CMD        "name-publish"
#define NAMEPUBLISHRESP_CMD    "name-publish-response"
#define NAMEUNPUBLISH_CMD      "name-unpublish"
#define NAMEUNPUBLISHRESP_CMD  "name-unpublish-response"
#define NAMELOOKUP_CMD         "name-lookup"
#define NAMELOOKUPRESP_CMD     "name-lookup-response"
#define SPAWN_CMD              "spawn"
#define SPAWNRESP_CMD          "spawn-response"
#define RING_CMD               "ring"
#define RINGRESP_CMD           "ring-response"

#define GETMYKVSNAME_CMD       "get_my_kvsname"
#define GETMYKVSNAMERESP_CMD   "my_kvsname"
#define CREATEKVS_CMD          "create_kvs"
#define DESTROYKVS_CMD         "destroy_kvs"
#define PUBLISHNAME_CMD        "publish_name"
#define UNPUBLISHNAME_CMD      "unpublish_name"
#define LOOKUPNAME_CMD         "lookup_name"
#define PUBLISHRESULT_CMD      "publish_result"
#define UNPUBLISHRESULT_CMD    "unpublish_result"
#define LOOKUPRESULT_CMD       "lookup_result"
#define MCMD_CMD               "mcmd"


#define CMD_KEY           "cmd"
#define PMIJOBID_KEY      "pmijobid"
#define PMIRANK_KEY       "pmirank"
#define SRCID_KEY         "srcid"
#define THREADED_KEY      "threaded"
#define RC_KEY            "rc"
#define ERRMSG_KEY        "errmsg"
#define PMIVERSION_KEY    "pmi-version"
#define PMISUBVER_KEY     "pmi-subversion"
#define RANK_KEY          "rank"
#define SIZE_KEY          "size"
#define APPNUM_KEY        "appnum"
#define SPAWNERJOBID_KEY  "spawner-jobid"
#define DEBUGGED_KEY      "debugged"
#define PMIVERBOSE_KEY    "pmiverbose"
#define ISWORLD_KEY       "isworld"
#define MSG_KEY           "msg"
#define JOBID_KEY         "jobid"
#define KVSCOPY_KEY       "kvscopy"
#define KEY_KEY           "key"
#define VALUE_KEY         "value"
#define FOUND_KEY         "found"
#define WAIT_KEY          "wait"
#define NAME_KEY          "name"
#define PORT_KEY          "port"
#define THRID_KEY         "thrid"
#define INFOKEYCOUNT_KEY  "infokeycount"
#define INFOKEY_KEY       "infokey"
#define INFOVAL_KEY       "infoval"
#define FOUND_KEY         "found"
#define NCMDS_KEY         "ncmds"
#define PREPUTCOUNT_KEY   "preputcount"
#define PPKEY_KEY         "ppkey"
#define PPVAL_KEY         "ppval"
#define SUBCMD_KEY        "subcmd"
#define MAXPROCS_KEY      "maxprocs"
#define ARGC_KEY          "argc"
#define ARGV_KEY          "argv"
#define INFOKEYCOUNT_KEY  "infokeycount"
#define ERRCODES_KEY      "errcodes"
#define SERVICE_KEY       "service"
#define INFO_KEY          "info"
#define RING_COUNT_KEY    "ring-count"
#define RING_LEFT_KEY     "ring-left"
#define RING_RIGHT_KEY    "ring-right"

#define TRUE_VAL          "TRUE"
#define FALSE_VAL         "FALSE"

#define JOB_ATTR_PROC_MAP       "PMI_process_mapping"
#define JOB_ATTR_UNIV_SIZE      "universeSize"
#define JOB_ATTR_NETINFO        "PMI_netinfo_of_task"
#define JOB_ATTR_RESV_PORTS     "mpi_reserved_ports"

/***********************************************************\
 * Environment variables
\***********************************************************/
#define PMI2_SRUN_PORT_ENV      "SLURM_PMI2_SRUN_PORT"
#define PMI2_STEP_NODES_ENV     "SLURM_PMI2_STEP_NODES"
#define PMI2_TREE_WIDTH_ENV     "SLURM_PMI2_TREE_WIDTH"
#define PMI2_PROC_MAPPING_ENV   "SLURM_PMI2_PROC_MAPPING"
#define PMI2_PMI_JOBID_ENV      "SLURM_PMI2_PMI_JOBID"
#define PMI2_SPAWN_SEQ_ENV      "SLURM_PMI2_SPAWN_SEQ"
#define PMI2_SPAWNER_JOBID_ENV  "SLURM_PMI2_SPAWNER_JOBID"
#define PMI2_SPAWNER_PORT_ENV   "SLURM_PMI2_SPAWNER_PORT"
#define PMI2_PREPUT_CNT_ENV     "SLURM_PMI2_PREPUT_COUNT"
#define PMI2_PPKEY_ENV          "SLURM_PMI2_PPKEY"
#define PMI2_PPVAL_ENV          "SLURM_PMI2_PPVAL"
#define SLURM_STEP_RESV_PORTS   "SLURM_STEP_RESV_PORTS"
#define PMIX_RING_TREE_WIDTH_ENV "SLURM_PMIX_RING_WIDTH"
/* old PMIv1 envs */
#define PMI2_PMI_DEBUGGED_ENV   "PMI_DEBUG"
#define PMI2_KVS_NO_DUP_KEYS_ENV "SLURM_PMI_KVS_NO_DUP_KEYS"


extern int handle_pmi1_cmd(int fd, int lrank);
extern int handle_pmi2_cmd(int fd, int lrank);

#endif /* _PMI_H */

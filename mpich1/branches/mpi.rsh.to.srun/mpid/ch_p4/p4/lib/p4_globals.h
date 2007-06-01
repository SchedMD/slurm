#ifdef GLOBAL
#define PUBLIC
#else
#define PUBLIC extern
#endif
    
/* Debugging information */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

PUBLIC char procgroup_file[256];
PUBLIC char bm_outfile[100];
PUBLIC char rm_outfile_head[100];
PUBLIC char whoami_p4[100];
PUBLIC int  p4_debug_level, p4_remote_debug_level;
PUBLIC char p4_wd[256];
PUBLIC char p4_myname_in_procgroup[MAXHOSTNAMELEN];
PUBLIC int  p4_rm_rank;
PUBLIC int  logging_flag;
PUBLIC int  execer_mynodenum;
PUBLIC char execer_id[132];
PUBLIC char execer_myhost[100];
PUBLIC int  execer_mynumprocs;
PUBLIC char execer_masthost[100];
#ifdef OLD_EXECER
PUBLIC char execer_jobname[100];
#endif
PUBLIC int  execer_mastport;
PUBLIC int  execer_numtotnodes;
PUBLIC struct p4_procgroup *execer_pg;
PUBLIC int  execer_starting_remotes;

/* Other global data */

PUBLIC char local_domain[100];
PUBLIC int  globmemsize;
PUBLIC int  sserver_port;
PUBLIC int  hand_start_remotes;

#ifdef SYSV_IPC
PUBLIC int sysv_num_shmids;
PUBLIC int sysv_shmid[P4_MAX_SYSV_SHMIDS];
PUBLIC char *sysv_shmat[P4_MAX_SYSV_SHMIDS];
PUBLIC int sysv_semid0;
#endif

#ifdef SP1_EUI
PUBLIC int eui_numtasks;
PUBLIC int eui_mynode;
#endif

#ifdef SP1_EUIH
PUBLIC int euih_numtasks;
PUBLIC int euih_mynode;
#endif

#if defined(SGI)  &&  defined(VENDOR_IPC)
#include <sys/param.h>
PUBLIC usptr_t *p4_sgi_usptr;
PUBLIC char p4_sgi_shared_arena_filename[MAXPATHLEN];
#endif

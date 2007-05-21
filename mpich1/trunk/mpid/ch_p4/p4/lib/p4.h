#ifndef _P4_H_
#define _P4_H_

#include <ctype.h>
#include <stdio.h>

#if defined(RS6000)  ||  defined(SYMMETRY_PTX)
#include <sys/select.h>
#endif

/* for xdr  -  includes netinet/in.h and sys/types.h */
/* HP-UX does not properly guard rpc/rpc.h from multiple inclusion */
/* We do not check for this in configure and have to put this check outside */
/* of the rpc code so rpc.h does not get included twice */

/* This definition is available if needed; there is currently no configure 
   check for it */
#ifdef NEEDS_INT64_DEFINITION
typedef long long int int64_t;
#endif

#if defined(HAVE_RPC_RPC_H) && !defined(INCLUDED_RPC_RPC_H)
#include <rpc/rpc.h>      
#define INCLUDED_RPC_RPC_H
#endif
/* Some systems DO NOT include netinet! */
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKETVAR_H
#include <sys/socketvar.h>
#endif

#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#if defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)
/* Include for some timer structures and for the time function call */
#include <time.h>
#endif
#include <pwd.h>
#include <fcntl.h>

#include "p4_config.h"
#include "p4_MD.h"
#include "p4_mon.h"
#include "p4_sr.h"

#define HOSTNAME_LEN 64

#define P4_TRUE 1
#define P4_FALSE 0

struct p4_procgroup_entry {
    int numslaves_in_group;
    int rm_rank;               /* Rank of the remote master for this entry */
    char host_name[HOSTNAME_LEN];
    char slave_full_pathname[256];
    char username[16];
};

#define P4_MAX_PROCGROUP_ENTRIES 1024
struct p4_procgroup {
    struct p4_procgroup_entry entries[P4_MAX_PROCGROUP_ENTRIES];
    int num_entries;
};

/* Provide prototypes for the functions */
#include "p4_funcs.h"

#ifndef P4_DPRINTFL
#define p4_dprintfl
#endif

#include "alog.h"

#include "usc.h"
#define p4_ustimer() usc_clock()
#define p4_usrollover() usc_MD_rollover_val

#endif

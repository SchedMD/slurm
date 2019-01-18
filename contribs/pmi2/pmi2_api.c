/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2007 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 *  Copyright (C) 2013      Intel, Inc.
 */

#include "pmi2_util.h"
#include "slurm/pmi2.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#ifndef MAXHOSTNAME
#define MAXHOSTNAME 256
#endif

#define PMII_EXIT_CODE -1

#define PMI_VERSION    2
#define PMI_SUBVERSION 0

#define MAX_INT_STR_LEN 11 /* number of digits in MAX_UINT + 1 */

typedef enum {
    PMI2_UNINITIALIZED = 0,
    SINGLETON_INIT_BUT_NO_PM = 1,
    NORMAL_INIT_WITH_PM,
    SINGLETON_INIT_WITH_PM
} PMI2State;

static PMI2State PMI2_initialized = PMI2_UNINITIALIZED;

static int PMI2_debug = 0;
static int PMI2_fd = -1;
static int PMI2_size = 1;
static int PMI2_rank = 0;

static pthread_mutex_t pmi2_mutex = PTHREAD_MUTEX_INITIALIZER;

/* XXX DJG the "const"s on both of these functions and the Keyvalpair
 * struct are wrong in the isCopy==TRUE case! */
/* init_kv_str -- fills in keyvalpair.  val is required to be a
   null-terminated string.  isCopy is set to FALSE, so caller must
   free key and val memory, if necessary.
*/
static void init_kv_str(PMI2_Keyvalpair *kv, const char key[], const char val[])
{
    kv->key = key;
    kv->value = val;
    kv->valueLen = strlen(val);
    kv->isCopy = 0/*FALSE*/;
}

/* same as init_kv_str, but strdup's the key and val first, and sets isCopy=TRUE */
static void init_kv_strdup(PMI2_Keyvalpair *kv, const char key[], const char val[])
{
    /* XXX DJG could be slightly more efficient */
    init_kv_str(kv, strdup(key), strdup(val));
    kv->isCopy = 1/*TRUE*/;
}

/* same as init_kv_strdup, but converts val into a string first */
/* XXX DJG could be slightly more efficient */
static void init_kv_strdup_int(PMI2_Keyvalpair *kv, const char key[], int val)
{
    char tmpbuf[32] = {0};
    int rc = PMI2_SUCCESS;

    rc = snprintf(tmpbuf, sizeof(tmpbuf), "%d", val);
    PMI2U_Assert(rc >= 0);
    init_kv_strdup(kv, key, tmpbuf);
}

/* initializes the key with ("%s%d", key_prefix, suffix), uses a string value */
/* XXX DJG could be slightly more efficient */
static void init_kv_strdup_intsuffix(PMI2_Keyvalpair *kv, const char key_prefix[], int suffix, const char val[])
{
    char tmpbuf[256/*XXX HACK*/] = {0};
    int rc = PMI2_SUCCESS;

    rc = snprintf(tmpbuf, sizeof(tmpbuf), "%s%d", key_prefix, suffix);
    PMI2U_Assert(rc >= 0);
    init_kv_strdup(kv, tmpbuf, val);
}


static int getPMIFD(void);
static int PMIi_ReadCommandExp( int fd, PMI2_Command *cmd, const char *exp, int* rc, const char **errmsg );
static int PMIi_ReadCommand( int fd, PMI2_Command *cmd );

static int PMIi_WriteSimpleCommand( int fd, PMI2_Command *resp, const char cmd[], PMI2_Keyvalpair *pairs[], int npairs);
static int PMIi_WriteSimpleCommandStr( int fd, PMI2_Command *resp, const char cmd[], ...);
static int PMIi_InitIfSingleton(void);
static int PMII_singinit(void);

static void freepairs(PMI2_Keyvalpair** pairs, int npairs);
static int getval(PMI2_Keyvalpair *const pairs[], int npairs, const char *key,  const char **value, int *vallen);
static int getvalint(PMI2_Keyvalpair *const pairs[], int npairs, const char *key, int *val);
static int getvalptr(PMI2_Keyvalpair *const pairs[], int npairs, const char *key, void *val);
static int getvalbool(PMI2_Keyvalpair *const pairs[], int npairs, const char *key, int *val);

static int accept_one_connection(int list_sock);
static int GetResponse(const char request[], const char expectedCmd[], int checkRc);

static void dump_PMI2_Command(PMI2_Command *cmd);
static void dump_PMI2_Keyvalpair(PMI2_Keyvalpair *kv);
static void phony(void);

typedef struct pending_item
{
    struct pending_item *next;
    PMI2_Command *cmd;
} pending_item_t;

pending_item_t *pendingq_head = NULL;
pending_item_t *pendingq_tail = NULL;

/* phony()
 * Collect unused functions which make the
 * gcc complain ;defined but not used'
 */
static void
phony(void)
{
	if (0) {
		accept_one_connection(0);
		GetResponse(NULL, NULL, 0);
		dump_PMI2_Command(NULL);
		PMII_singinit();
	}
}

static inline void ENQUEUE(PMI2_Command *cmd)
{
    pending_item_t *pi = malloc(sizeof(pending_item_t));

    pi->next = NULL;
    pi->cmd = cmd;

    if (pendingq_head == NULL) {
        pendingq_head = pendingq_tail = pi;
    } else {
        pendingq_tail->next = pi;
        pendingq_tail = pi;
    }
}

static inline int SEARCH_REMOVE(PMI2_Command *cmd)
{
    pending_item_t *pi, *prev;

    pi = pendingq_head;
    if (pi->cmd == cmd) {
        pendingq_head = pi->next;
        if (pendingq_head == NULL)
            pendingq_tail = NULL;
        free(pi);
        return 1;
    }
    prev = pi;
    pi = pi->next;

    for ( ; pi ; pi = pi->next) {
        if (pi->cmd == cmd) {
            prev->next = pi->next;
            if (prev->next == NULL)
                pendingq_tail = prev;
            free(pi);
            return 1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
/* PMI-2 API Routines                                                        */
/* ------------------------------------------------------------------------- */
int PMI2_Init(int *spawned, int *size, int *rank, int *appnum)
{
    int pmi2_errno = PMI2_SUCCESS;
    char *p;
    char buf[PMI2_MAXLINE], cmdline[PMI2_MAXLINE];
    char *jobid;
    char *pmiid;
    int ret;

    PMI2U_printf("[BEGIN]");

    /* Get the value of PMI2_DEBUG from the environment if possible, since
       we may have set it to help debug the setup process */
    p = getenv("PMI2_DEBUG");
    if (p) PMI2_debug = atoi(p);

    /* Get the fd for PMI commands; if none, we're a singleton */
    pmi2_errno = getPMIFD();
    if (pmi2_errno) PMI2U_ERR_POP(pmi2_errno);

    if (PMI2_fd == -1) {
	    /* Singleton init: Process not started with mpiexec,
     		   so set size to 1, rank to 0 */
		PMI2_size = 1;
		PMI2_rank = 0;
		*spawned = 0;
		*size = PMI2_size;
		*rank = PMI2_rank;
		*appnum = -1;

		PMI2_initialized = SINGLETON_INIT_BUT_NO_PM;
		goto fn_exit;
    }

    /* do initial PMI1 init */
    ret = snprintf(buf, PMI2_MAXLINE, "cmd=init pmi_version=%d pmi_subversion=%d\n", PMI_VERSION, PMI_SUBVERSION);
    PMI2U_ERR_CHKANDJUMP(ret < 0, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "failed to generate init line");

    ret = PMI2U_writeline(PMI2_fd, buf);
    PMI2U_ERR_CHKANDJUMP(ret < 0, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_init_send");

    ret = PMI2U_readline(PMI2_fd, buf, PMI2_MAXLINE);
    PMI2U_ERR_CHKANDJUMP(ret < 0, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_initack %s", strerror(pmi2_errno));

    PMI2U_parse_keyvals(buf);
    cmdline[0] = 0;
    PMI2U_getval("cmd", cmdline, PMI2_MAXLINE);
    PMI2U_ERR_CHKANDJUMP(strncmp(cmdline, "response_to_init", PMI2_MAXLINE) != 0,  pmi2_errno, PMI2_ERR_OTHER, "**bad_cmd");

    PMI2U_getval("rc", buf, PMI2_MAXLINE);
    if (strncmp(buf, "0", PMI2_MAXLINE) != 0) {
        char buf1[PMI2_MAXLINE];
        PMI2U_getval("pmi_version", buf, PMI2_MAXLINE);
        PMI2U_getval("pmi_subversion", buf1, PMI2_MAXLINE);
        PMI2U_ERR_SETANDJUMP(pmi2_errno, PMI2_ERR_OTHER, "**pmi2_version %s %s %d %d", buf, buf1, PMI_VERSION, PMI_SUBVERSION);
    }

    PMI2U_printf("do full PMI2 init ...");
    /* do full PMI2 init */
    {
        PMI2_Keyvalpair pairs[3];
        PMI2_Keyvalpair *pairs_p[] = { pairs, pairs+1, pairs+2 };
        int npairs = 0;
        int isThreaded = 0;
        const char *errmsg;
        int rc;
        int found;
        int version, subver;
        const char *spawner_jobid;
        int spawner_jobid_len;
        PMI2_Command cmd = {0};
        int debugged;
        int PMI2_pmiverbose;


        jobid = getenv("PMI_JOBID");
        if (jobid) {
            init_kv_str(&pairs[npairs], PMIJOBID_KEY, jobid);
            ++npairs;
        }

        pmiid = getenv("PMI_ID");
        if (pmiid) {
            init_kv_str(&pairs[npairs], SRCID_KEY, pmiid);
            ++npairs;
        }
        else {
            pmiid = getenv("PMI_RANK");
            if (pmiid) {
                init_kv_str(&pairs[npairs], PMIRANK_KEY, pmiid);
                PMI2_rank = strtol(pmiid, NULL, 10);
                ++npairs;
            }
        }

        init_kv_str(&pairs[npairs], THREADED_KEY, isThreaded ? "TRUE" : "FALSE");
        ++npairs;

        pmi2_errno = PMIi_WriteSimpleCommand(PMI2_fd, 0, FULLINIT_CMD, pairs_p, npairs); /* don't pass in thread id for init */
        if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommand");

        /* Read auth-response */
        /* Send auth-response-complete */

        /* Read fullinit-response */
        pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, FULLINITRESP_CMD, &rc, &errmsg);
        if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
        PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_fullinit %s", errmsg ? errmsg : "unknown");

        found = getvalint(cmd.pairs, cmd.nPairs, PMIVERSION_KEY, &version);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        found = getvalint(cmd.pairs, cmd.nPairs, PMISUBVER_KEY, &subver);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        found = getvalint(cmd.pairs, cmd.nPairs, RANK_KEY, rank);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        found = getvalint(cmd.pairs, cmd.nPairs, SIZE_KEY, size);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
        PMI2_size = *size;

        found = getvalint(cmd.pairs, cmd.nPairs, APPNUM_KEY, appnum);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        found = getval(cmd.pairs, cmd.nPairs, SPAWNERJOBID_KEY, &spawner_jobid, &spawner_jobid_len);
        PMI2U_ERR_CHKANDJUMP(found == -1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
        if (found)
            *spawned = TRUE;
        else
            *spawned = FALSE;

        debugged = 0;
        found = getvalbool(cmd.pairs, cmd.nPairs, DEBUGGED_KEY, &debugged);
        PMI2U_ERR_CHKANDJUMP(found == -1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
        PMI2_debug |= debugged;

        PMI2_pmiverbose = 0;
        found = getvalbool(cmd.pairs, cmd.nPairs, PMIVERBOSE_KEY, &PMI2_pmiverbose);
        PMI2U_ERR_CHKANDJUMP(found == -1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        free(cmd.command);
        freepairs(cmd.pairs, cmd.nPairs);
    }

    if (! PMI2_initialized) {
        PMI2_initialized = NORMAL_INIT_WITH_PM;
        pmi2_errno = PMI2_SUCCESS;
    }


    phony();

fn_exit:
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Finalize(void)
{
    int pmi2_errno = PMI2_SUCCESS;
    int rc;
    const char *errmsg;
    PMI2_Command cmd = {0};

    PMI2U_printf("[BEGIN]");

    if (PMI2_initialized > SINGLETON_INIT_BUT_NO_PM) {
        pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, FINALIZE_CMD, NULL);
        if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
        pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, FINALIZERESP_CMD, &rc, &errmsg);
        if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
        PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_finalize %s", errmsg ? errmsg : "unknown");

        free(cmd.command);
        freepairs(cmd.pairs, cmd.nPairs);

        shutdown(PMI2_fd, SHUT_RDWR);
        close(PMI2_fd);
    }

fn_exit:
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Initialized(void)
{
    /* Turn this into a logical value (1 or 0). This allows us
       to use PMI2_initialized to distinguish between initialized with
       an PMI service (e.g., via mpiexec) and the singleton init,
       which has no PMI service */
    return (PMI2_initialized != 0);
}

int PMI2_Abort(int flag, const char msg[])
{
	if (msg)
		PMI2U_printf("aborting job:\n%s", msg);

    PMIi_WriteSimpleCommandStr(PMI2_fd, NULL, ABORT_CMD, ISWORLD_KEY,
                               flag ? TRUE_VAL : FALSE_VAL,
                               MSG_KEY, ((msg == NULL) ? "": msg), NULL);

    exit(flag);
    return PMI2_SUCCESS;
}

int PMI2_Job_Spawn(int count, const char * cmds[],
                   int argcs[], const char ** argvs[],
                   const int maxprocs[],
                   const int info_keyval_sizes[],
                   const struct MPID_Info *info_keyval_vectors[],
                   int preput_keyval_size,
                   const struct MPID_Info *preput_keyval_vector[],
                   char jobId[], int jobIdSize,
                   int errors[])
{
    int  i,rc,spawncnt,total_num_processes,num_errcodes_found;
    int found;
    const char *jid;
    int jidlen;
    char tempbuf[PMI2_MAXLINE];
    char *lead, *lag;
    int spawn_rc;
    const char *errmsg = NULL;
    PMI2_Command resp_cmd  = {0};
    int pmi2_errno = 0;
    PMI2_Keyvalpair **pairs_p = NULL;
    int npairs = 0;
    int total_pairs = 0;

    PMI2U_printf("[BEGIN]");

    /* Connect to the PM if we haven't already */
    if (PMIi_InitIfSingleton() != 0) return -1;

    total_num_processes = 0;

/* XXX DJG from Pavan's email:
cmd=spawn;thrid=string;ncmds=count;preputcount=n;ppkey0=name;ppval0=string;...;\
        subcmd=spawn-exe1;maxprocs=n;argc=narg;argv0=name;\
                argv1=name;...;infokeycount=n;infokey0=key;\
                infoval0=string;...;\
(... one subcmd for each executable ...)
*/

    /* FIXME overall need a better interface for building commands!
     * Need to be able to append commands, and to easily accept integer
     * valued arguments.  Memory management should stay completely out
     * of mind when writing a new PMI command impl like this! */

    /* Calculate the total number of keyval pairs that we need.
     *
     * The command writing utility adds "cmd" and "thrid" fields for us,
     * don't include them in our count. */
    total_pairs = 2; /* ncmds,preputcount */
    total_pairs += (3 * count); /* subcmd,maxprocs,argc */
    total_pairs += (2 * preput_keyval_size); /* ppkeyN,ppvalN */
    for (spawncnt = 0; spawncnt < count; ++spawncnt) {
        total_pairs += argcs[spawncnt]; /* argvN */
        if (info_keyval_sizes) {
            total_pairs += 1;  /* infokeycount */
            total_pairs += 2 * info_keyval_sizes[spawncnt]; /* infokeyN,infovalN */
        }
    }

    pairs_p = malloc(total_pairs * sizeof(PMI2_Keyvalpair*));
    /* individiually allocating instead of batch alloc b/c freepairs assumes it */
    for (i = 0; i < total_pairs; ++i) {
        /* FIXME we are somehow still leaking some of this memory */
        pairs_p[i] = malloc(sizeof(PMI2_Keyvalpair));
        PMI2U_Assert(pairs_p[i]);
    }

    init_kv_strdup_int(pairs_p[npairs++], "ncmds", count);

    init_kv_strdup_int(pairs_p[npairs++], "preputcount", preput_keyval_size);
    for (i = 0; i < preput_keyval_size; ++i) {
        init_kv_strdup_intsuffix(pairs_p[npairs++], "ppkey", i, preput_keyval_vector[i]->key);
        init_kv_strdup_intsuffix(pairs_p[npairs++], "ppval", i, preput_keyval_vector[i]->value);
    }

    for (spawncnt = 0; spawncnt < count; ++spawncnt)
    {
        total_num_processes += maxprocs[spawncnt];

        init_kv_strdup(pairs_p[npairs++], "subcmd", cmds[spawncnt]);
        init_kv_strdup_int(pairs_p[npairs++], "maxprocs", maxprocs[spawncnt]);

        init_kv_strdup_int(pairs_p[npairs++], "argc", argcs[spawncnt]);
        for (i = 0; i < argcs[spawncnt]; ++i) {
            init_kv_strdup_intsuffix(pairs_p[npairs++], "argv", i, argvs[spawncnt][i]);
        }

        if (info_keyval_sizes) {
            init_kv_strdup_int(pairs_p[npairs++], "infokeycount", info_keyval_sizes[spawncnt]);
            for (i = 0; i < info_keyval_sizes[spawncnt]; ++i) {
                init_kv_strdup_intsuffix(pairs_p[npairs++], "infokey", i, info_keyval_vectors[spawncnt][i].key);
                init_kv_strdup_intsuffix(pairs_p[npairs++], "infoval", i, info_keyval_vectors[spawncnt][i].value);
            }
        }
    }

    if (npairs < total_pairs) { PMI2U_printf("about to fail assertion, npairs=%d total_pairs=%d", npairs, total_pairs); }
    PMI2U_Assert(npairs == total_pairs);

    pmi2_errno = PMIi_WriteSimpleCommand(PMI2_fd, &resp_cmd, "spawn", pairs_p, npairs);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommand");

    freepairs(pairs_p, npairs);
    pairs_p = NULL;

    /* XXX DJG TODO release any upper level MPICH2 critical sections */
    rc = PMIi_ReadCommandExp(PMI2_fd, &resp_cmd, "spawn-response", &spawn_rc, &errmsg);
    if (rc != 0) { return PMI2_FAIL; }

    /* XXX DJG TODO deal with the response */
    PMI2U_Assert(errors != NULL);

    if (jobId && jobIdSize) {
        found = getval(resp_cmd.pairs, resp_cmd.nPairs, JOBID_KEY, &jid, &jidlen);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
        MPIU_Strncpy(jobId, jid, jobIdSize);
    }

    if (PMI2U_getval("errcodes", tempbuf, PMI2_MAXLINE)) {
        num_errcodes_found = 0;
        lag = &tempbuf[0];
        do {
            lead = strchr(lag, ',');
            if (lead) *lead = '\0';
            errors[num_errcodes_found++] = atoi(lag);
            lag = lead + 1; /* move past the null char */
            PMI2U_Assert(num_errcodes_found <= total_num_processes);
        } while (lead != NULL);
        PMI2U_Assert(num_errcodes_found == total_num_processes);
    }
    else {
        /* gforker doesn't return errcodes, so we'll just pretend that means
           that it was going to send all `0's. */
        for (i = 0; i < total_num_processes; ++i) {
            errors[i] = 0;
        }
    }

fn_fail:
    free(resp_cmd.command);
    freepairs(resp_cmd.pairs, resp_cmd.nPairs);
    if (pairs_p) freepairs(pairs_p, npairs);

    PMI2U_printf("[END]");
    return pmi2_errno;
}

int PMI2_Job_GetId(char jobid[], int jobid_size)
{
    int pmi2_errno = PMI2_SUCCESS;
    int found;
    const char *jid;
    int jidlen;
    int rc;
    const char *errmsg;
    PMI2_Command cmd = {0};

    PMI2U_printf("[BEGIN]");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, JOBGETID_CMD, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, JOBGETIDRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_jobgetid %s", errmsg ? errmsg : "unknown");

    found = getval(cmd.pairs, cmd.nPairs, JOBID_KEY, &jid, &jidlen);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

    MPIU_Strncpy(jobid, jid, jobid_size);

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Job_GetRank(int* rank)
{
    *rank = PMI2_rank;
    return PMI2_SUCCESS;
}

int PMI2_Info_GetSize(int* size)
{
    *size = PMI2_size;
    return PMI2_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME PMI2_Job_Connect
#undef FCNAME
#define FCNAME PMI2DI_QUOTE(FUNCNAME)

int PMI2_Job_Connect(const char jobid[], PMI2_Connect_comm_t *conn)
{
    int pmi2_errno = PMI2_SUCCESS;
    PMI2_Command cmd = {0};
    int found;
    int kvscopy;
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, JOBCONNECT_CMD, JOBID_KEY, jobid, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, JOBCONNECTRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_jobconnect %s", errmsg ? errmsg : "unknown");

    found = getvalbool(cmd.pairs, cmd.nPairs, KVSCOPY_KEY, &kvscopy);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

    PMI2U_ERR_CHKANDJUMP(kvscopy, pmi2_errno, PMI2_ERR_OTHER, "**notimpl");

 fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    PMI2U_printf("[END]");
    return pmi2_errno;
 fn_fail:
    goto fn_exit;
}

int PMI2_Job_Disconnect(const char jobid[])
{
    int pmi2_errno = PMI2_SUCCESS;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, JOBDISCONNECT_CMD, JOBID_KEY, jobid, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, JOBDISCONNECTRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_jobdisconnect %s", errmsg ? errmsg : "unknown");

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMIX_Ring(const char value[], int *rank, int *ranks, char left[], char right[], int maxvalue)
{
    int pmi2_errno = PMI2_SUCCESS;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;
    int found;
    const char *kvsvalue;
    int kvsvallen;

    PMI2U_printf("[BEGIN PMI2_Ring]");

    /* for singleton mode, set rank and ranks, copy input to output buffers */
    if (PMI2_initialized == SINGLETON_INIT_BUT_NO_PM) {
        *rank  = 0;
        *ranks = 1;
        MPIU_Strncpy(left,  value, maxvalue);
        MPIU_Strncpy(right, value, maxvalue);
        goto fn_exit_singleton;
    }

    /* send message: cmd=ring_in, count=1, left=value, right=value */
    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, RING_CMD,
	RING_COUNT_KEY,   "1",
	RING_LEFT_KEY,  value,
	RING_RIGHT_KEY, value,
	NULL);
    if (pmi2_errno) PMI2U_ERR_POP(pmi2_errno);

    /* wait for reply: cmd=ring_out, rc=0|1, count=rank, left=leftval, right=rightval */
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, RINGRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_ring %s", errmsg ? errmsg : "unknown");

    /* get our rank from the count key */
    found = getvalint(cmd.pairs, cmd.nPairs, RING_COUNT_KEY, rank);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

    /* set size of ring (just number of procs in job) */
    *ranks = PMI2_size;

    /* lookup left value and copy to caller's buffer */
    found = getval(cmd.pairs, cmd.nPairs, RING_LEFT_KEY, &kvsvalue, &kvsvallen);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
    MPIU_Strncpy(left, kvsvalue, maxvalue);

    /* lookup right value and copy to caller's buffer */
    found = getval(cmd.pairs, cmd.nPairs, RING_RIGHT_KEY, &kvsvalue, &kvsvallen);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
    MPIU_Strncpy(right, kvsvalue, maxvalue);

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
fn_exit_singleton:
    PMI2U_printf("[END PMI2_Ring]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_KVS_Put(const char key[], const char value[])
{
    int pmi2_errno = PMI2_SUCCESS;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, KVSPUT_CMD, KEY_KEY, key, VALUE_KEY, value, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, KVSPUTRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_kvsput %s", errmsg ? errmsg : "unknown");

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_KVS_Fence(void)
{
    int pmi2_errno = PMI2_SUCCESS;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, KVSFENCE_CMD, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, KVSFENCERESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_kvsfence %s", errmsg ? errmsg : "unknown");

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_KVS_Get(const char *jobid, int src_pmi_id, const char key[], char value [], int maxValue, int *valLen)
{
    int pmi2_errno = PMI2_SUCCESS;
    int found, keyfound;
    const char *kvsvalue;
    int kvsvallen;
    PMI2_Command cmd = {0};
    int rc;
    int ret;
    char src_pmi_id_str[256];
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    snprintf(src_pmi_id_str, sizeof(src_pmi_id_str), "%d", src_pmi_id);

    pmi2_errno = PMIi_InitIfSingleton();
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_InitIfSingleton");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, KVSGET_CMD, JOBID_KEY, jobid, SRCID_KEY, src_pmi_id_str, KEY_KEY, key, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, KVSGETRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_kvsget %s", errmsg ? errmsg : "unknown");

    found = getvalbool(cmd.pairs, cmd.nPairs, FOUND_KEY, &keyfound);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
    PMI2U_ERR_CHKANDJUMP(!keyfound, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_kvsget_notfound");

    found = getval(cmd.pairs, cmd.nPairs, VALUE_KEY, &kvsvalue, &kvsvallen);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

    ret = MPIU_Strncpy(value, kvsvalue, maxValue);
    *valLen = ret ? -kvsvallen : kvsvallen;

 fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
 fn_fail:
    goto fn_exit;
}

int PMI2_Info_GetNodeAttr(const char name[], char value[], int valuelen, int *flag, int waitfor)
{
    int pmi2_errno = PMI2_SUCCESS;
    int found;
    const char *kvsvalue;
    int kvsvallen;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_InitIfSingleton();
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_InitIfSingleton");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, GETNODEATTR_CMD, KEY_KEY, name, WAIT_KEY, waitfor ? "TRUE" : "FALSE", NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, GETNODEATTRRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_getnodeattr %s", errmsg ? errmsg : "unknown");

    found = getvalbool(cmd.pairs, cmd.nPairs, FOUND_KEY, flag);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
    if (*flag) {
        found = getval(cmd.pairs, cmd.nPairs, VALUE_KEY, &kvsvalue, &kvsvallen);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        MPIU_Strncpy(value, kvsvalue, valuelen);
    }

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Info_GetNodeAttrIntArray(const char name[], int array[], int arraylen, int *outlen, int *flag)
{
    int pmi2_errno = PMI2_SUCCESS;
    int found;
    const char *kvsvalue;
    int kvsvallen;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;
    int i;
    const char *valptr;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_InitIfSingleton();
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_InitIfSingleton");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, GETNODEATTR_CMD, KEY_KEY, name, WAIT_KEY, "FALSE", NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, GETNODEATTRRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_getnodeattr %s", errmsg ? errmsg : "unknown");

    found = getvalbool(cmd.pairs, cmd.nPairs, FOUND_KEY, flag);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
    if (*flag) {
        found = getval(cmd.pairs, cmd.nPairs, VALUE_KEY, &kvsvalue, &kvsvallen);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        valptr = kvsvalue;
        i = 0;
        rc = sscanf(valptr, "%d", &array[i]);
        PMI2U_ERR_CHKANDJUMP(rc != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "unable to parse intarray");
        ++i;
        while ((valptr = strchr(valptr, ',')) && i < arraylen) {
            ++valptr; /* skip over the ',' */
            rc = sscanf(valptr, "%d", &array[i]);
            PMI2U_ERR_CHKANDJUMP(rc != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "unable to parse intarray");
            ++i;
        }

        *outlen = i;
    }

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Info_PutNodeAttr(const char name[], const char value[])
{
    int pmi2_errno = PMI2_SUCCESS;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, PUTNODEATTR_CMD, KEY_KEY, name, VALUE_KEY, value, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, PUTNODEATTRRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_putnodeattr %s", errmsg ? errmsg : "unknown");

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Info_GetJobAttr(const char name[], char value[], int valuelen, int *flag)
{
    int pmi2_errno = PMI2_SUCCESS;
    int found;
    const char *kvsvalue;
    int kvsvallen;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_InitIfSingleton();
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_InitIfSingleton");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, GETJOBATTR_CMD, KEY_KEY, name, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, GETJOBATTRRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_getjobattr %s", errmsg ? errmsg : "unknown");

    found = getvalbool(cmd.pairs, cmd.nPairs, FOUND_KEY, flag);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

    if (*flag) {
        found = getval(cmd.pairs, cmd.nPairs, VALUE_KEY, &kvsvalue, &kvsvallen);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        MPIU_Strncpy(value, kvsvalue, valuelen);
    }

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Info_GetJobAttrIntArray(const char name[], int array[], int arraylen, int *outlen, int *flag)
{
    int pmi2_errno = PMI2_SUCCESS;
    int found;
    const char *kvsvalue;
    int kvsvallen;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;
    int i;
    const char *valptr;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_InitIfSingleton();
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_InitIfSingleton");

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, GETJOBATTR_CMD, KEY_KEY, name, NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, GETJOBATTRRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_getjobattr %s", errmsg ? errmsg : "unknown");

    found = getvalbool(cmd.pairs, cmd.nPairs, FOUND_KEY, flag);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");
    if (*flag) {
        found = getval(cmd.pairs, cmd.nPairs, VALUE_KEY, &kvsvalue, &kvsvallen);
        PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

        valptr = kvsvalue;
        i = 0;
        rc = sscanf(valptr, "%d", &array[i]);
        PMI2U_ERR_CHKANDJUMP(rc != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "unable to parse intarray");
        ++i;
        while ((valptr = strchr(valptr, ',')) && i < arraylen) {
            ++valptr; /* skip over the ',' */
            rc = sscanf(valptr, "%d", &array[i]);
            PMI2U_ERR_CHKANDJUMP(rc != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "unable to parse intarray");
            ++i;
        }

        *outlen = i;
    }

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Nameserv_publish(const char service_name[], const PMI2U_Info *info_ptr, const char port[])
{
    int pmi2_errno = PMI2_SUCCESS;
    PMI2_Command cmd = {0};
    int rc;
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    /* ignoring infokey functionality for now */
    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, NAMEPUBLISH_CMD,
                                            NAME_KEY, service_name, PORT_KEY, port,
                                            INFOKEYCOUNT_KEY, "0", NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, NAMEPUBLISHRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_nameservpublish %s", errmsg ? errmsg : "unknown");


fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}


int PMI2_Nameserv_lookup(const char service_name[], const PMI2U_Info *info_ptr,
                         char port[], int portLen)
{
    int pmi2_errno = PMI2_SUCCESS;
    int found;
    int rc;
    PMI2_Command cmd = {0};
    int plen;
    const char *errmsg;
    const char *found_port;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    /* ignoring infos for now */
    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, NAMELOOKUP_CMD,
                                            NAME_KEY, service_name, INFOKEYCOUNT_KEY, "0", NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, NAMELOOKUPRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_nameservlookup %s", errmsg ? errmsg : "unknown");

    found = getval(cmd.pairs, cmd.nPairs, VALUE_KEY, &found_port, &plen);
    PMI2U_ERR_CHKANDJUMP(!found, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_nameservlookup %s", "not found");
    MPIU_Strncpy(port, found_port, portLen);

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

int PMI2_Nameserv_unpublish(const char service_name[],
                            const PMI2U_Info *info_ptr)
{
    int pmi2_errno = PMI2_SUCCESS;
    int rc;
    PMI2_Command cmd = {0};
    const char *errmsg;

    PMI2U_printf("[BEGIN]");
    pthread_mutex_lock(&pmi2_mutex);

    pmi2_errno = PMIi_WriteSimpleCommandStr(PMI2_fd, &cmd, NAMEUNPUBLISH_CMD,
                                            NAME_KEY, service_name, INFOKEYCOUNT_KEY, "0", NULL);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_WriteSimpleCommandStr");
    pmi2_errno = PMIi_ReadCommandExp(PMI2_fd, &cmd, NAMEUNPUBLISHRESP_CMD, &rc, &errmsg);
    if (pmi2_errno) PMI2U_ERR_SETANDJUMP(1, pmi2_errno, "PMIi_ReadCommandExp");
    PMI2U_ERR_CHKANDJUMP(rc, pmi2_errno, PMI2_ERR_OTHER, "**pmi2_nameservunpublish %s", errmsg ? errmsg : "unknown");

fn_exit:
    free(cmd.command);
    freepairs(cmd.pairs, cmd.nPairs);
    pthread_mutex_unlock(&pmi2_mutex);
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}

/* ------------------------------------------------------------------------- */
/* Service Routines */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/*
 * PMIi_ReadCommand - Reads an entire command from the PMI socket.  This
 * routine blocks the thread until the command is read.
 *
 * PMIi_WriteSimpleCommand - Write a simple command to the PMI socket; this
 * allows printf - style arguments.  This blocks the thread until the buffer
 * has been written (for fault-tolerance, we may want to keep it around
 * in case of PMI failure).
 *
 * PMIi_WaitFor - Wait for a particular PMI command request to complete.
 */
/* ------------------------------------------------------------------------- */

/* frees all of the keyvals pointed to by a keyvalpair* array and the array iteself*/
static void freepairs(PMI2_Keyvalpair** pairs, int npairs)
{
    int i;

    if (!pairs)
        return;

    for (i = 0; i < npairs; ++i)
        if (pairs[i]->isCopy) {
            /* FIXME casts are here to suppress legitimate constness warnings */
            free((void *)pairs[i]->key);
            free((void *)pairs[i]->value);
            free(pairs[i]);
        }
    free(pairs);
}

/* getval & friends -- these functions search the pairs list for a
 * matching key, set val appropriately and return 1.  If no matching
 * key is found, 0 is returned.  If the value is invalid, -1 is returned */

static int getval(PMI2_Keyvalpair *const pairs[], int npairs, const char *key,  const char **value, int *vallen)
{
    int i;

    for (i = 0; i < npairs; ++i)
        if (strncmp(key, pairs[i]->key, PMI2_MAX_KEYLEN) == 0) {
            *value = pairs[i]->value;
            *vallen = pairs[i]->valueLen;
            return 1;
        }
    return 0;
}

static int getvalint(PMI2_Keyvalpair *const pairs[], int npairs, const char *key, int *val)
{
    int found;
    const char *value;
    int vallen;
    int ret;
    /* char *endptr; */

    found = getval(pairs, npairs, key, &value, &vallen);
    if (found != 1)
        return found;

    if (vallen == 0)
        return -1;

    ret = sscanf(value, "%d", val);
    if (ret != 1)
        return -1;

    /* *val = strtoll(value, &endptr, 0); */
    /* if (endptr - value != vallen) */
    /*     return -1; */

    return 1;
}

static int getvalptr(PMI2_Keyvalpair *const pairs[], int npairs, const char *key, void *val)
{
    int found;
    const char *value;
    int vallen;
    int ret;
    void **val_ = val;
    /* char *endptr; */

    found = getval(pairs, npairs, key, &value, &vallen);
    if (found != 1)
        return found;

    if (vallen == 0)
        return -1;

    ret = sscanf(value, "%p", val_);
    if (ret != 1)
        return -1;

    /* *val_ = (void *)(PMI2R_Upint)strtoll(value, &endptr, 0); */
    /* if (endptr - value != vallen) */
    /*     return -1; */

    return 1;
}


static int getvalbool(PMI2_Keyvalpair *const pairs[], int npairs, const char *key, int *val)
{
    int found;
    const char *value;
    int vallen;


    found = getval(pairs, npairs, key, &value, &vallen);
    if (found != 1)
        return found;

    if (strlen("TRUE") == vallen && !strncmp(value, "TRUE", vallen))
        *val = 1/*TRUE*/;
    else if (strlen("FALSE") == vallen && !strncmp(value, "FALSE", vallen))
        *val = 0/*FALSE*/;
    else
        return -1;

    return 1;
}



/* parse_keyval(cmdptr, len, key, val, vallen)
   Scans through buffer specified by cmdptr looking for the first key and value.
     IN/OUT cmdptr - IN: pointer to buffer; OUT: pointer to byte after the ';' terminating the value
     IN/OUT len    - IN: length of buffer; OUT: length of buffer not read
     OUT    key    - pointer to null-terminated string containing the key
     OUT    val    - pointer to string containing the value
     OUT    vallen - length of the value string

   This function will modify the buffer passed through cmdptr to
   insert '\0' following the key, and to replace escaped ';;' with
   ';'.
 */
static int parse_keyval(char **cmdptr, int *len, char **key, char **val, int *vallen)
{
    int pmi2_errno = PMI2_SUCCESS;
    char *c = *cmdptr;
    char *d;

    /*PMI2U_printf("[BEGIN]");*/

    /* find key */
    *key = c; /* key is at the start of the buffer */
    while (*len && *c != '=') {
        --*len;
        ++c;
    }
    PMI2U_ERR_CHKANDJUMP(*len == 0, pmi2_errno, PMI2_ERR_OTHER, "**bad_keyval");
    PMI2U_ERR_CHKANDJUMP((c - *key) > PMI2_MAX_KEYLEN, pmi2_errno, PMI2_ERR_OTHER, "**bad_keyval");
    *c = '\0'; /* terminate the key string */

    /* skip over the '=' */
    --*len;
    ++c;

    /* find val */
    *val = d = c; /* val is next */
    while (*len) {
        if (*c == ';') { /* handle escaped ';' */
            if (*(c+1) != ';')
                break;
            else
            {
                --*len;
                ++c;
            }
        }
        --*len;
        *(d++) = *(c++);
    }
    PMI2U_ERR_CHKANDJUMP(*len == 0, pmi2_errno, PMI2_ERR_OTHER, "**bad_keyval");
    PMI2U_ERR_CHKANDJUMP((d - *val) > PMI2_MAX_VALLEN, pmi2_errno, PMI2_ERR_OTHER, "**bad_keyval");
    *c = '\0'; /* terminate the val string */
    *vallen = d - *val;

    *cmdptr = c+1; /* skip over the ';' */
    --*len;

 fn_exit:
    /*PMI2U_printf("[END]");*/
    return pmi2_errno;
 fn_fail:
    goto fn_exit;
}

static int create_keyval(PMI2_Keyvalpair **kv, const char *key, const char *val, int vallen)
{
    int pmi2_errno = PMI2_SUCCESS;
    int key_len = strlen(key);
    char *key_p;
    char *value_p;
    PMI2U_CHKMEM_DECL(3);

    /*PMI2U_printf("[BEGIN]");*/
    /*PMI2U_printf("[BEGIN] create_keyval(%p, %s, %s, %d)", kv, key, val, vallen);*/

    PMI2U_CHKMEM_MALLOC(*kv, PMI2_Keyvalpair *, sizeof(PMI2_Keyvalpair), pmi2_errno, "pair");

    PMI2U_CHKMEM_MALLOC(key_p, char *, key_len+1, pmi2_errno, "key");
    MPIU_Strncpy(key_p, key, key_len+1);
    key_p[key_len] = '\0';

    PMI2U_CHKMEM_MALLOC(value_p, char *, vallen+1, pmi2_errno, "value");
    memcpy(value_p, val, vallen);
    value_p[vallen] = '\0';

    (*kv)->key = key_p;
    (*kv)->value = value_p;
    (*kv)->valueLen = vallen;
    (*kv)->isCopy = 1/*TRUE*/;

fn_exit:
    PMI2U_CHKMEM_COMMIT();
    /*PMI2U_printf("[END]");*/
    return pmi2_errno;
fn_fail:
    PMI2U_CHKMEM_REAP();
    goto fn_exit;
}


/* Note that we fill in the fields in a command that is provided.
   We may want to share these routines with the PMI version 2 server */
int PMIi_ReadCommand( int fd, PMI2_Command *cmd )
{
    int pmi2_errno = PMI2_SUCCESS;
    char cmd_len_str[PMII_COMMANDLEN_SIZE+1];
    int cmd_len, remaining_len, vallen = 0;
    char *c, *cmd_buf = NULL;
    char *key, *val = NULL;
    ssize_t nbytes;
    ssize_t offset;
    int num_pairs;
    int pair_index;
    char *command = NULL;
    int nPairs;
    int found;
    PMI2_Keyvalpair **pairs = NULL;
    PMI2_Command *target_cmd;

    PMI2U_printf("[BEGIN]");

    memset(cmd_len_str, 0, sizeof(cmd_len_str));

#ifdef MPICH_IS_THREADED
    MPIU_THREAD_CHECK_BEGIN;
    {
        MPID_Thread_mutex_lock(&mutex);

        while (blocked && !cmd->complete)
            MPID_Thread_cond_wait(&cond, &mutex);

        if (cmd->complete) {
            MPID_Thread_mutex_unlock(&mutex);
            goto fn_exit;
        }

        blocked = 1/*TRUE*/;
        MPID_Thread_mutex_unlock(&mutex);
    }
    MPIU_THREAD_CHECK_END;
#endif

    do {

        /* get length of cmd */
        offset = 0;
        do
        {
            do {
                nbytes = read(fd, &cmd_len_str[offset], PMII_COMMANDLEN_SIZE - offset);
            } while (nbytes == -1 && errno == EINTR);

            PMI2U_ERR_CHKANDJUMP(nbytes <= 0, pmi2_errno, PMI2_ERR_OTHER, "**read %s", strerror(errno));

            offset += nbytes;
        }
        while (offset < PMII_COMMANDLEN_SIZE);

        cmd_len = atoi(cmd_len_str);

        cmd_buf = malloc(cmd_len+1);
        if (!cmd_buf) PMI2U_CHKMEM_SETERR(pmi2_errno, cmd_len+1, "cmd_buf");

        memset(cmd_buf, 0, cmd_len+1);

        /* get command */
        offset = 0;
        do
        {
            do {
                nbytes = read(fd, &cmd_buf[offset], cmd_len - offset);
            } while (nbytes == -1 && errno == EINTR);

            PMI2U_ERR_CHKANDJUMP(nbytes <= 0, pmi2_errno, PMI2_ERR_OTHER, "**read %s", strerror(errno));

            offset += nbytes;
        }
        while (offset < cmd_len);

        PMI2U_printf("PMI received (cmdlen %d):  %s", cmd_len, cmd_buf);

        /* count number of "key=val;" */
        c = cmd_buf;
        remaining_len = cmd_len;
        num_pairs = 0;

        while (remaining_len > 0) {
            while (remaining_len && *c != ';') {
                --remaining_len;
                ++c;
            }
            if (*c == ';' && *(c+1) == ';') {
                remaining_len -= 2;
                c += 2;
            } else {
                ++num_pairs;
                --remaining_len;
                ++c;
            }
        }

        c = cmd_buf;
        remaining_len = cmd_len;
        pmi2_errno = parse_keyval(&c, &remaining_len, &key, &val, &vallen);
        if (pmi2_errno) PMI2U_ERR_POP(pmi2_errno);

        PMI2U_ERR_CHKANDJUMP(strncmp(key, "cmd", PMI2_MAX_KEYLEN) != 0, pmi2_errno, PMI2_ERR_OTHER, "**bad_cmd");

        command = malloc(vallen+1);
        if (!command) PMI2U_CHKMEM_SETERR(pmi2_errno, vallen+1, "command");
        memcpy(command, val, vallen);
        val[vallen] = '\0';

        nPairs = num_pairs-1;  /* num_pairs-1 because the first pair is the command */

        pairs = malloc(sizeof(PMI2_Keyvalpair *) * nPairs);
        if (!pairs) PMI2U_CHKMEM_SETERR(pmi2_errno, sizeof(PMI2_Keyvalpair *) * nPairs, "pairs");

        pair_index = 0;
        while (remaining_len > 0)
        {
            PMI2_Keyvalpair *pair;

            pmi2_errno = parse_keyval(&c, &remaining_len, &key, &val, &vallen);
            if (pmi2_errno) PMI2U_ERR_POP(pmi2_errno);

            pmi2_errno = create_keyval(&pair, key, val, vallen);
            if (pmi2_errno) PMI2U_ERR_POP(pmi2_errno);

            pairs[pair_index] = pair;
            ++pair_index;
        }

        found = getvalptr(pairs, nPairs, THRID_KEY, &target_cmd);
        if (!found) /* if there's no thrid specified, assume it's for you */
            target_cmd = cmd;
        else
            if (PMI2_debug && SEARCH_REMOVE(target_cmd) == 0) {
                int i;

                PMI2U_printf("command=%s", command);
                for (i = 0; i < nPairs; ++i)
                    dump_PMI2_Keyvalpair(pairs[i]);
            }

        target_cmd->command = command;
        target_cmd->nPairs = nPairs;
        target_cmd->pairs = pairs;
        target_cmd->complete = 1/*TRUE*/;

#ifdef MPICH_IS_THREADED
        target_cmd->complete = 1/*TRUE*/;
#endif

        if (cmd_buf) free(cmd_buf);
        cmd_buf = NULL;
    } while (!cmd->complete);

#ifdef MPICH_IS_THREADED
    MPIU_THREAD_CHECK_BEGIN;
    {
        MPID_Thread_mutex_lock(&mutex);
        blocked = 0/*FALSE*/;
        MPID_Thread_cond_broadcast(&cond);
        MPID_Thread_mutex_unlock(&mutex);
    }
    MPIU_THREAD_CHECK_END;
#endif

fn_exit:
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    if (cmd_buf) free(cmd_buf);
    goto fn_exit;
}

/* PMIi_ReadCommandExp -- reads a command checks that it matches the
 * expected command string exp, and parses the return code */
int PMIi_ReadCommandExp( int fd, PMI2_Command *cmd, const char *exp, int* rc, const char **errmsg )
{
    int pmi2_errno = PMI2_SUCCESS;
    int found;
    int msglen;

    PMI2U_printf("[BEGIN]");

    pmi2_errno = PMIi_ReadCommand(fd, cmd);
    if (pmi2_errno) PMI2U_ERR_POP(pmi2_errno);

    PMI2U_ERR_CHKANDJUMP(strncmp(cmd->command, exp, strlen(exp)) != 0,  pmi2_errno, PMI2_ERR_OTHER, "**bad_cmd");

    found = getvalint(cmd->pairs, cmd->nPairs, RC_KEY, rc);
    PMI2U_ERR_CHKANDJUMP(found != 1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

    found = getval(cmd->pairs, cmd->nPairs, ERRMSG_KEY, errmsg, &msglen);
    PMI2U_ERR_CHKANDJUMP(found == -1, pmi2_errno, PMI2_ERR_OTHER, "**intern");

    if (!found) *errmsg = NULL;

fn_exit:
    PMI2U_printf("[END]");
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}


int PMIi_WriteSimpleCommand( int fd, PMI2_Command *resp, const char cmd[], PMI2_Keyvalpair *pairs[], int npairs)
{
    int pmi2_errno = PMI2_SUCCESS;
    char cmdbuf[PMII_MAX_COMMAND_LEN];
    char cmdlenbuf[PMII_COMMANDLEN_SIZE+1];
    char *c = cmdbuf;
    int ret;
    int remaining_len = PMII_MAX_COMMAND_LEN;
    int cmdlen;
    int i;
    ssize_t nbytes;
    ssize_t offset;
    int pair_index;

    PMI2U_printf("[BEGIN]");

    /* leave space for length field */
    memset(c, ' ', PMII_COMMANDLEN_SIZE);
    c += PMII_COMMANDLEN_SIZE;

    PMI2U_ERR_CHKANDJUMP(strlen(cmd) > PMI2_MAX_VALLEN, pmi2_errno, PMI2_ERR_OTHER, "**cmd_too_long");

    /* Subtract the PMII_COMMANDLEN_SIZE to prevent
     * certain implementation of snprintf() to
     * segfault when zero out the buffer.
     * PMII_COMMANDLEN_SIZE must be added later on
     * back again to send out the right protocol
     * message size.
     */
    remaining_len -= PMII_COMMANDLEN_SIZE;

    ret = snprintf(c, remaining_len, "cmd=%s;", cmd);
    PMI2U_ERR_CHKANDJUMP(ret >= remaining_len, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "Ran out of room for command");
    c += ret;
    remaining_len -= ret;

#ifdef MPICH_IS_THREADED
    MPIU_THREAD_CHECK_BEGIN;
    if (resp) {
        ret = snprintf(c, remaining_len, "thrid=%p;", resp);
        PMI2U_ERR_CHKANDJUMP(ret >= remaining_len, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "Ran out of room for command");
        c += ret;
        remaining_len -= ret;
    }
    MPIU_THREAD_CHECK_END;
#endif

    for (pair_index = 0; pair_index < npairs; ++pair_index) {
        /* write key= */
        PMI2U_ERR_CHKANDJUMP(strlen(pairs[pair_index]->key) > PMI2_MAX_KEYLEN, pmi2_errno, PMI2_ERR_OTHER, "**key_too_long");
        ret = snprintf(c, remaining_len, "%s=", pairs[pair_index]->key);
        PMI2U_ERR_CHKANDJUMP(ret >= remaining_len, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "Ran out of room for command");
        c += ret;
        remaining_len -= ret;

        /* write value and escape ;'s as ;; */
        PMI2U_ERR_CHKANDJUMP(pairs[pair_index]->valueLen > PMI2_MAX_VALLEN, pmi2_errno, PMI2_ERR_OTHER, "**val_too_long");
        for (i = 0; i < pairs[pair_index]->valueLen; ++i) {
            if (pairs[pair_index]->value[i] == ';') {
                *c = ';';
                ++c;
                --remaining_len;
            }
            *c = pairs[pair_index]->value[i];
            ++c;
            --remaining_len;
        }

        /* append ; */
        *c = ';';
        ++c;
        --remaining_len;
    }

    /* prepend the buffer length stripping off the trailing '\0'
     * Add back the PMII_COMMANDLEN_SIZE to get the correct
     * protocol size.
     */
    cmdlen = PMII_MAX_COMMAND_LEN - (remaining_len + PMII_COMMANDLEN_SIZE);
    ret = snprintf(cmdlenbuf, sizeof(cmdlenbuf), "%d", cmdlen);
    PMI2U_ERR_CHKANDJUMP(ret >= PMII_COMMANDLEN_SIZE, pmi2_errno, PMI2_ERR_OTHER, "**intern %s", "Command length won't fit in length buffer");

    memcpy(cmdbuf, cmdlenbuf, ret);

    cmdbuf[cmdlen+PMII_COMMANDLEN_SIZE] = '\0'; /* silence valgrind warnings in PMI2U_printf */
    PMI2U_printf("PMI sending: %s", cmdbuf);


 #ifdef MPICH_IS_THREADED
    MPIU_THREAD_CHECK_BEGIN;
    {
        MPID_Thread_mutex_lock(&mutex);

        while (blocked)
            MPID_Thread_cond_wait(&cond, &mutex);

        blocked = 1/*TRUE*/;
        MPID_Thread_mutex_unlock(&mutex);
    }
    MPIU_THREAD_CHECK_END;
#endif

    if (PMI2_debug)
        ENQUEUE(resp);

    offset = 0;
    do {
        do {
            nbytes = write(fd, &cmdbuf[offset], cmdlen + PMII_COMMANDLEN_SIZE - offset);
        } while (nbytes == -1 && errno == EINTR);

        PMI2U_ERR_CHKANDJUMP(nbytes <= 0, pmi2_errno, PMI2_ERR_OTHER, "**write %s", strerror(errno));

        offset += nbytes;
    } while (offset < cmdlen + PMII_COMMANDLEN_SIZE);
#ifdef MPICH_IS_THREADED
    MPIU_THREAD_CHECK_BEGIN;
    {
        MPID_Thread_mutex_lock(&mutex);
        blocked = 0/*FALSE*/;
        MPID_Thread_cond_broadcast(&cond);
        MPID_Thread_mutex_unlock(&mutex);
    }
    MPIU_THREAD_CHECK_END;
#endif

fn_fail:
    goto fn_exit;
fn_exit:
    PMI2U_printf("[END]");
    return pmi2_errno;
}

int PMIi_WriteSimpleCommandStr(int fd, PMI2_Command *resp, const char cmd[], ...)
{
    int pmi2_errno = PMI2_SUCCESS;
    va_list ap;
    PMI2_Keyvalpair *pairs;
    PMI2_Keyvalpair **pairs_p;
    int npairs;
    int i;
    const char *key;
    const char *val;
    PMI2U_CHKMEM_DECL(2);

    PMI2U_printf("[BEGIN]");

    npairs = 0;
    va_start(ap, cmd);
    while ((key = va_arg(ap, const char*))) {
        val = va_arg(ap, const char*);
        ++npairs;
    }
    va_end(ap);

    /* allocates n+1 pairs in case npairs is 0, avoiding unnecessary warning logs */
    PMI2U_CHKMEM_MALLOC(pairs, PMI2_Keyvalpair*, (sizeof(PMI2_Keyvalpair) * (npairs+1)), pmi2_errno, "pairs");
    PMI2U_CHKMEM_MALLOC(pairs_p, PMI2_Keyvalpair**, (sizeof(PMI2_Keyvalpair*) * (npairs+1)), pmi2_errno, "pairs_p");

    i = 0;
    va_start(ap, cmd);
    while ((key = va_arg(ap, const char *))) {
        val = va_arg(ap, const char *);
        pairs_p[i] = &pairs[i];
        pairs[i].key = key;
        pairs[i].value = val;
        if (val == NULL)
	        pairs[i].valueLen = 0;
        else
	        pairs[i].valueLen = strlen(val);
        pairs[i].isCopy = 0/*FALSE*/;
        ++i;
    }
    va_end(ap);

    pmi2_errno = PMIi_WriteSimpleCommand(fd, resp, cmd, pairs_p, npairs);
    if (pmi2_errno) PMI2U_ERR_POP(pmi2_errno);

fn_exit:
    PMI2U_printf("[END]");
    PMI2U_CHKMEM_FREEALL();
    return pmi2_errno;
fn_fail:
    goto fn_exit;
}


/*
 * This code allows a program to contact a host/port for the PMI socket.
 */
#include <sys/types.h>
#include <sys/param.h>

/* sockaddr_in (Internet) */
#include <netinet/in.h>
/* TCP_NODELAY */
#include <netinet/tcp.h>

/* sockaddr_un (Unix) */
#include <sys/un.h>

/* defs of gethostbyname */
#include <netdb.h>

/* fcntl, F_GET/SETFL */
#include <fcntl.h>

/* This is really IP!? */
#ifndef TCP
#define TCP 0
#endif

/* stub for connecting to a specified host/port instead of using a
   specified fd inherited from a parent process */
static int PMII_Connect_to_pm( char *hostname, int portnum )
{
    struct hostent     *hp;
    struct sockaddr_in sa;
    int                fd;
    int                optval = 1;
    int                q_wait = 1;

    hp = gethostbyname( hostname );
    if (!hp) {
    PMI2U_printf("Unable to get host entry for %s", hostname );
    return -1;
    }

    memset( (void *)&sa, 0, sizeof(sa) );
    /* POSIX might define h_addr_list only and node define h_addr */
#ifdef HAVE_H_ADDR_LIST
    memcpy( (void *)&sa.sin_addr, (void *)hp->h_addr_list[0], hp->h_length);
#else
    memcpy( (void *)&sa.sin_addr, (void *)hp->h_addr, hp->h_length);
#endif
    sa.sin_family = hp->h_addrtype;
    sa.sin_port   = htons( (unsigned short) portnum );

    fd = socket( AF_INET, SOCK_STREAM, TCP );
    if (fd < 0) {
    PMI2U_printf("Unable to get AF_INET socket" );
    return -1;
    }

    if (setsockopt( fd, IPPROTO_TCP, TCP_NODELAY,
            (char *)&optval, sizeof(optval) )) {
    perror( "Error calling setsockopt:" );
    }

    /* We wait here for the connection to succeed */
    if (connect( fd, (struct sockaddr *)&sa, sizeof(sa) ) < 0) {
    switch (errno) {
    case ECONNREFUSED:
        PMI2U_printf("connect failed with connection refused" );
        /* (close socket, get new socket, try again) */
        if (q_wait)
        close(fd);
        return -1;

    case EINPROGRESS: /*  (nonblocking) - select for writing. */
        break;

    case EISCONN: /*  (already connected) */
        break;

    case ETIMEDOUT: /* timed out */
        PMI2U_printf("connect failed with timeout" );
        return -1;

    default:
        PMI2U_printf("connect failed with errno %d", errno );
        return -1;
    }
    }

    return fd;
}

/* ------------------------------------------------------------------------- */
/*
 * Singleton Init.
 *
 * MPI-2 allows processes to become MPI processes and then make MPI calls,
 * such as MPI_Comm_spawn, that require a process manager (this is different
 * than the much simpler case of allowing MPI programs to run with an
 * MPI_COMM_WORLD of size 1 without an mpiexec or process manager).
 *
 * The process starts when either the client or the process manager contacts
 * the other.  If the client starts, it sends a singinit command and
 * waits for the server to respond with its own singinit command.
 * If the server start, it send a singinit command and waits for the
 * client to respond with its own singinit command
 *
 * client sends singinit with these required values
 *   pmi_version=<value of PMI_VERSION>
 *   pmi_subversion=<value of PMI_SUBVERSION>
 *
 * and these optional values
 *   stdio=[yes|no]
 *   authtype=[none|shared|<other-to-be-defined>]
 *   authstring=<string>
 *
 * server sends singinit with the same required and optional values as
 * above.
 *
 * At this point, the protocol is now the same in both cases, and has the
 * following components:
 *
 * server sends singinit_info with these required fields
 *   versionok=[yes|no]
 *   stdio=[yes|no]
 *   kvsname=<string>
 *
 * The client then issues the init command (see PMII_getmaxes)
 *
 * cmd=init pmi_version=<val> pmi_subversion=<val>
 *
 * and expects to receive a
 *
 * cmd=response_to_init rc=0 pmi_version=<val> pmi_subversion=<val>
 *
 * (This is the usual init sequence).
 *
 */
/* ------------------------------------------------------------------------- */
/* This is a special routine used to re-initialize PMI when it is in
   the singleton init case.  That is, the executable was started without
   mpiexec, and PMI2_Init returned as if there was only one process.

   Note that PMI routines should not call PMII_singinit; they should
   call PMIi_InitIfSingleton(), which both connects to the process mangager
   and sets up the initial KVS connection entry.
*/

static int PMII_singinit(void)
{
    return 0;
}

/* Promote PMI to a fully initialized version if it was started as
   a singleton init */
static int PMIi_InitIfSingleton(void)
{
    return 0;
}

static int accept_one_connection(int list_sock)
{
    int gotit, new_sock;
    struct sockaddr_in from;
    socklen_t len;

    len = sizeof(from);
    gotit = 0;
    while ( ! gotit )
    {
        new_sock = accept(list_sock, (struct sockaddr *)&from, &len);
        if (new_sock == -1)
        {
            if (errno == EINTR)
                continue;   /* interrupted? If so, try again */
            else
            {
                PMI2U_printf("accept failed in accept_one_connection");
                exit (-1);
            }
        }
        else gotit = 1;
    }

    return new_sock;
}


/* Get the FD to use for PMI operations.  If a port is used, rather than
   a pre-established FD (i.e., via pipe), this routine will handle the
   initial handshake.
*/
static int getPMIFD(void)
{
    int pmi2_errno = PMI2_SUCCESS;
    char *p;

    /* Set the default */
    PMI2_fd = -1;

    p = getenv("PMI_FD");
    if (p) {
        PMI2_fd = atoi(p);
        goto fn_exit;
    }

    p = getenv( "PMI_PORT" );
    if (p) {
    int portnum;
    char hostname[MAXHOSTNAME+1];
    char *pn, *ph;

    /* Connect to the indicated port (in format hostname:portnumber)
       and get the fd for the socket */

    /* Split p into host and port */
    pn = p;
    ph = hostname;
    while (*pn && *pn != ':' && (ph - hostname) < MAXHOSTNAME) {
        *ph++ = *pn++;
    }
    *ph = 0;

        PMI2U_ERR_CHKANDJUMP(*pn != ':', pmi2_errno, PMI2_ERR_OTHER, "**pmi2_port %s", p);

        portnum = atoi( pn+1 );
        /* FIXME: Check for valid integer after : */
        /* This routine only gets the fd to use to talk to
           the process manager. The handshake below is used
           to setup the initial values */
        PMI2_fd = PMII_Connect_to_pm( hostname, portnum );
        PMI2U_ERR_CHKANDJUMP(PMI2_fd < 0, pmi2_errno, PMI2_ERR_OTHER, "**connect_to_pm %s %d", hostname, portnum);
    }

    /* OK to return success for singleton init */

 fn_exit:
    return pmi2_errno;
 fn_fail:
    goto fn_exit;
}

/* ----------------------------------------------------------------------- */
/*
 * This function is used to request information from the server and check
 * that the response uses the expected command name.  On a successful
 * return from this routine, additional PMI2U_getval calls may be used
 * to access information about the returned value.
 *
 * If checkRc is true, this routine also checks that the rc value returned
 * was 0.  If not, it uses the "msg" value to report on the reason for
 * the failure.
 */
static int GetResponse( const char request[], const char expectedCmd[],
            int checkRc )
{
    int err = 0;

    return err;
}

static void dump_PMI2_Keyvalpair(PMI2_Keyvalpair *kv)
{
    PMI2U_printf("key      = %s", kv->key);
    PMI2U_printf("value    = %s", kv->value);
    PMI2U_printf("valueLen = %d", kv->valueLen);
    PMI2U_printf("isCopy   = %s", kv->isCopy ? "TRUE" : "FALSE");
}

static void dump_PMI2_Command(PMI2_Command *cmd)
{
    int i;

    PMI2U_printf("cmd    = %s", cmd->command);
    PMI2U_printf("nPairs = %d", cmd->nPairs);

    for (i = 0; i < cmd->nPairs; ++i)
        dump_PMI2_Keyvalpair(cmd->pairs[i]);
}

#if 0
/*  Currently disabled
 *
 *_connect_to_stepd()
 *
 * If the user requests PMI2_CONNECT_TO_SERVER do
 * connect over the PMI2_SUN_PATH unix socket.
 */
static int
_connect_to_stepd(int s)
{
    struct sockaddr_un addr;
    int cc;
    char *usock;
    char *p;
    int myrank;
    int n;

    usock = getenv("PMI2_SUN_PATH");
    if (usock == NULL)
	return -1;

    cc = socket(PF_UNIX, SOCK_STREAM, 0);
    if (cc < 0) {
	perror("socket()");
	return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));

    addr.sun_family = AF_UNIX;
    sprintf(addr.sun_path, "%s", usock);

    if (connect(cc, (struct sockaddr *)&addr,
		sizeof(struct sockaddr_un)) != 0) {
	perror("connect()");
	close(cc);
	return -1;
    }

    /* The very first thing we have to tell the pmi
     * server is our rank, so he can associate our
     * file descriptor with our rank.
     */
    p = getenv("PMI_RANK");
    if (p == NULL) {
	fprintf(stderr, "%s: failed to get PMI_RANK from env\n", __func__);
	close(cc);
	return -1;
    }

    myrank = atoi(p);
    n = write(cc, &myrank, sizeof(int));
    if (n != sizeof(int)) {
	perror("write()");
	close(cc);
	return -1;
    }

    /* close() all socket and return
     * the new.
     */
    close(s);

    return cc;
}
#endif

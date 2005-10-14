/*****************************************************************************\
 *  sacct.c - job accounting reports for SLURM's jobacct/log plugin
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*
 * HISTORY
 * $Log$
 * Revision 1.7  2005/06/29 20:41:23  da
 * New Tag HP's patch applied for mutex issue in jobacct.
 *
 * Revision 1.6  2005/06/24 01:19:52  jette
 * Additional documenation for job accounting. Some bug fixes too. All from
 * Andy Riebs/HP.
 *
 * Revision 1.5  2005/06/11 00:49:43  jette
 * Get all the latest accounting software patches.
 *
 * Revision 1.1  2005/06/01 17:26:11  jette
 * Extensive mods checked it for HP work, see NEWS for details.
 *
 * Revision 1.4  2005/05/31 20:28:20  riebs
 * Include "errors" in the default sacct display.
 *
 * Revision 1.3  2005/05/27 17:37:43  riebs
 * Don't discard JOB_START and JOB_END records when selecting on job
 * steps. ("sacct -J 246.1" would report "Error: No JOB_START record for
 * job 246"). This was not a problem when --dump was specified.
 *
 * Revision 1.2  2005/05/19 20:42:11  riebs
 * 1. Fix problem of double-flush of .expired records when scontrol is
 *    unavailable
 * 2. Handle "--expire=1d" as "expire everything through yesterday,"
 *    rather than "expire everything up to  exactly 24 hours ago."
 *
 * Revision 1.1  2005/05/13 20:11:14  riebs
 * Add the jobacct plugins and the sacct utility, and upgrade to
 * slurm-0.4.22-1.
 *
 * Revision 1.9  2005/05/03 12:38:35  riebs
 * Implement "sacct --expire" to facilitate logfile rotation.
 *
 * Revision 1.8  2005/04/15 23:01:39  riebs
 * Check in the changes for dynamic SLURM job accounting (that is, the
 * code to capture runtime data for psize and vsize).
 *
 * Revision 1.7  2005/04/11 21:05:44  riebs
 * Check in a work-around for a getopt_long() bug.
 *
 * Revision 1.6  2005/04/07 18:43:46  riebs
 * Fix a hand full of off-by-one problems, and add --version
 *
 * Revision 1.2  2005/04/07 18:41:42  riebs
 * updat the rev
 *
 * Revision 1.1  2005/04/07 18:33:08  riebs
 * Initial revision
 *
 * Revision 1.5  2005/04/06 19:37:40  riebs
 * Clean up sacct output.
 *
 * Revision 1.4  2005/04/05 15:28:01  riebs
 * - Implement --all
 * - Clean up output formatting for elapsed time
 * - Expand output field for jobname
 *
 * Revision 1.3  2005/04/02 19:46:44  riebs
 * Remove the setuid-related code, initialize job[].cstatus properly, fix
 * formatting of the JOB_STEP record, and fix printing of elapsed time.
 *
 * Revision 1.2  2005/04/01 17:10:43  riebs
 * Replace the Perl version of sacct with sacct.c
 *
 * Revision 1.1  2005/03/31 21:57:45  riebs
 * Initial revision
 *
 * Revision 1.1  2005/03/31 21:19:28  riebs
 * Add the .c version of sacct to CVS in anticipation of retiring the
 * .pl version.
 *
 * Revision 1.8  2005/03/31 19:25:19  riebs
 * Solid version of sacct with all functionality!
 *
 * Revision 1.7  2005/03/31 13:24:41  riebs
 * Good version of formatted_dump implemented.
 *
 * Revision 1.6  2005/03/31 00:33:45  riebs
 * Pretty good implementation of fdump now.
 *
 * Revision 1.5  2005/03/30 23:57:31  riebs
 * Version that handles all print fields.
 *
 * Revision 1.4  2005/03/30 20:51:13  riebs
 * A precautionary version before I radically change
 * the fields struct.
 *
 * Revision 1.3  2005/03/30 18:26:24  riebs
 * Pretty solid version of --dump
 *
 * Revision 1.2  2005/03/29 14:43:20  riebs
 * All data are aggregated; just need to print it now!
 *
 * Revision 1.1  2005/03/28 18:21:26  andy
 * Initial revision
 *
 * Revision 1.1  2005/03/28 16:18:38  riebs
 * Initial revision
 *
 *
 * $EndLog$
 */

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "src/common/getopt.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#define ERROR 2

/* slurmd uses "(uint32_t) -2" to track data for batch allocations
 * which have no logical jobsteps. */
#define NOT_JOBSTEP "4294967294"

#define BRIEF_FIELDS "jobstep,status,error"
#define DEFAULT_FIELDS "jobstep,jobname,partition,ncpus,status,error"
#define BUFSIZE 1024
#define LONG_FIELDS "jobstep,usercpu,systemcpu,minflt,majflt,nprocs,ncpus,elapsed,status,error"

/* The following literals define how many significant characters
 * are used in a yyyymmddHHMMSS timestamp. */
#define MATCH_DAY 8
#define MATCH_HOUR 10
#define MATCH_MINUTE 12

#define MAX_JOBNAME_LENGTH 256
#define MAX_JOBS 100000
#define MAX_JOBSTEPS 500000
#define MAX_PARTITIONS 1000
#define MAX_PRINTFIELDS 100
#define MAX_STATES 100

#define MAX_RECORD_FIELDS 38

#define NO_JOBSTEP -1

#define SECONDS_IN_MINUTE 60
#define SECONDS_IN_HOUR (60*SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY (24*SECONDS_IN_HOUR)

#ifndef SLURM_CONFIG_FILE
#define SLURM_CONFIG_FILE "sacct was built with no default slurm.conf path"
#endif

/* Map field names to positions */

/* Fields common to all records */
#define F_JOB		0
#define F_PARTITION	1
#define F_SUBMITTED	2
#define F_STARTTIME	3
#define F_UIDGID	4
#define F_RESERVED1	5
#define F_RECTYPE	6
#define F_RECVERSION	7
#define F_NUMFIELDS	8

/* JOB_START fields */
#define F_UID		9
#define F_GID		10
#define F_JOBNAME	11
#define F_BATCH		12
#define F_PRIORITY	13
#define F_NCPUS		14
#define F_NODES		15

/* JOB_STEP fields */
#define F_JOBSTEP	9
#define F_FINISHED	10
#define F_CSTATUS	11
#define F_ERROR		12
#define F_NPROCS	13
/*define F_NCPUS 14 (defined above) */
#define F_ELAPSED	15
#define F_CPU_SEC	16
#define F_CPU_USEC	17
#define F_USER_SEC	18
#define F_USER_USEC	19
#define F_SYS_SEC	20
#define F_SYS_USEC	21
#define F_RSS		22
#define F_IXRSS		23
#define F_IDRSS		24
#define F_ISRSS		25
#define F_MINFLT	26
#define F_MAJFLT	27
#define F_NSWAP		28
#define F_INBLOCKS	29
#define F_OUBLOCKS	30
#define F_MSGSND	31
#define F_MSGRCV	32
#define F_NSIGNALS	33
#define F_NVCSW		34
#define F_NIVCSW	35
#define F_VSIZE		36
#define F_PSIZE		37

/* JOB_COMPLETION fields */
#define F_TOT_ELAPSED	9 
/*define F_FINISHED	10 */
/*define F_CSTATUS	11 */

/* On output, use fields 12-37 from JOB_STEP */

typedef enum {	HEADLINE,
	UNDERSCORE,
	JOB,
	JOBSTEP
} print_what_t;

int  cmp_long(const void *l1, const void *l2); 
char *decodeCstatus(char *cs);
void doDump(void);
void doExpire(void);
void doFdump(char* fields[]);
void doHelp(void);
void doList(void);
void dumpHeader(long j);
char *elapsedTime(long secs, long usecs);
int findJobRecord(long job);
int findJobstepRecord(long j, long jobstep);
void getData(void);
void getOptions(int argc, char **argv);
void helpFieldsMsg(void);
void helpMsg(void);
long  initJobStruct(long job, char *f[]);
void invalidSwitchCombo(char *good, char *bad);
void linkJobstep(long j, long js);
void openLogFile(void);
char *prefix_filename(char *path, char *prefix);
void printHeader(void);
void processJobStart(char *f[]);
void processJobStep(char *f[]);
void processJobTerminated(char *f[]);
void usage(void);
void *_my_malloc(size_t size);
void *_my_realloc(void *ptr, size_t size);

/* Field-specific print routines */
void printCpu(print_what_t which, long idx);
void printElapsed(print_what_t which, long idx);
void printError(print_what_t which, long idx);
void printFinished(print_what_t which, long idx);
void printGid(print_what_t which, long idx);
void printGroup(print_what_t which, long idx);
void printIdrss(print_what_t which, long idx);
void printInblocks(print_what_t which, long idx);
void printIsrss(print_what_t which, long idx);
void printIxrss(print_what_t which, long idx);
void printJob(print_what_t which, long idx);
void printJobName(print_what_t which, long idx);
void printJobStep(print_what_t which, long idx);
void printMajFlt(print_what_t which, long idx);
void printMinFlt(print_what_t which, long idx);
void printMsgRcv(print_what_t which, long idx);
void printMsgSnd(print_what_t which, long idx);
void printNcpus(print_what_t which, long idx);
void printNivcsw(print_what_t which, long idx);
void printNodes(print_what_t which, long idx);
void printNsignals(print_what_t which, long idx);
void printNswap(print_what_t which, long idx);
void printNprocs(print_what_t which, long idx);
void printNvcsw(print_what_t which, long idx);
void printOutBlocks(print_what_t which, long idx);
void printPartition(print_what_t which, long idx);
void printPsize(print_what_t which, long idx);
void printRss(print_what_t which, long idx);
void printStatus(print_what_t which, long idx);
void printSubmitted(print_what_t which, long idx);
void printSystemCpu(print_what_t which, long idx);
void printUid(print_what_t which, long idx);
void printUser(print_what_t which, long idx);
void printUserCpu(print_what_t which, long idx);
void printVsize(print_what_t which, long idx);

/*
 * Globals
 */

static struct {
	char *name;		/* Specified in --fields= */
	void (*printRoutine) ();	/* Who gets to print it? */
} fields[] = {
	{
	"cpu", &printCpu}, {
	"elapsed", &printElapsed}, {
	"error", &printError}, {
	"finished", &printFinished}, {
	"gid", &printGid}, {
	"group", &printGroup}, {
	"idrss", &printIdrss}, {
	"inblocks", &printInblocks}, {
	"isrss", &printIsrss}, {
	"ixrss", &printIxrss}, {
	"job", &printJob}, {
	"jobname", &printJobName}, {
	"jobstep", &printJobStep}, {
	"majflt", &printMajFlt}, {
	"minflt", &printMinFlt}, {
	"msgrcv", &printMsgRcv}, {
	"msgsnd", &printMsgSnd}, {
	"ncpus", &printNcpus}, {
	"nivcsw", &printNivcsw}, {
	"nodes", &printNodes}, {
	"nprocs", &printNprocs}, {
	"nsignals", &printNsignals}, {
	"nswap", &printNswap}, {
	"nvcsw", &printNvcsw}, {
	"outblocks", &printOutBlocks}, {
	"partition", &printPartition}, {
	"psize", &printPsize}, {
	"rss", &printRss}, {
	"status", &printStatus}, {
	"submitted", &printSubmitted}, {
	"systemcpu", &printSystemCpu}, {
	"uid", &printUid}, {
	"user", &printUser}, {
	"usercpu", &printUserCpu}, {
	"vsize", &printVsize}, {
	NULL, NULL}
};

FILE *logfile;

int inputError = 0,		/* Muddle through bad data, but complain! */
    expire_time_match = 0;	/* How much of the timestamp is significant
				   for --expire */
long lc = 0,			/* input file line counter */
     rc = 0;			/* return code */

long opt_expire = 0;		/* --expire= */

int  opt_debug = 0,		/* Option flags */
     opt_dump = 0,		/* --dump */
     opt_fdump = 0,		/* --formattted_dump */
     opt_gid = -1,		/* --gid (-1=wildcard, 0=root) */
     opt_header = 1,		/* can only be cleared */
     opt_help = 0,		/* --help */
     opt_long = 0,		/* --long */
     opt_lowmem = 0,		/* --low_memory */
     opt_total = 0,		/* --total */
     opt_uid = -1,		/* --uid (-1=wildcard, 0=root) */
     opt_verbose = 0;		/* --verbose */
char *opt_expire_timespec = NULL, /* --expire= */
     *opt_field_list = NULL,	/* --fields= */
     *opt_filein = NULL,	/* --file */
     *opt_job_list = NULL,	/* --jobs */
     *opt_jobstep_list = NULL,	/* --jobstep */
     *opt_partition_list = NULL,/* --partitions */
     *opt_state_list = NULL;	/* --states */

struct {
	char *job;
	char *step;
} jobstepsSelected[MAX_JOBSTEPS];
int NjobstepsSelected = 0;

char *partitionsSelected[MAX_PARTITIONS];
int NpartitionsSelected = 0;

int printFields[MAX_PRINTFIELDS],	/* Indexed into fields[] */
 NprintFields = 0;

char *statesSelected[MAX_STATES];
int NstatesSelected = 0;

struct {
	int	job_start_seen, job_step_seen, job_terminated_seen;	/* useful flags */
	long	first_jobstep; /* linked list into jobsteps */
	/* fields retrieved from JOB_START and JOB_TERMINATED records */
	long	job;
	char	*partition;
	char	submitted[15];	/* YYYYMMDDhhmmss */
	time_t	starttime;
	int	uid;
	int	gid;
	char	jobname[MAX_JOBNAME_LENGTH];
	int	batch;
	int	priority;
	long	ncpus;
	long	nprocs;
	char	*nodes;
	char	finished[15];
	char	cstatus[3];
	long	elapsed;
	long	tot_cpu_sec, tot_cpu_usec;
	long	tot_user_sec, tot_user_usec;
	long	tot_sys_sec, tot_sys_usec;
	long	rss, ixrss, idrss, isrss;
	long	minflt, majflt;
	long	nswap;
	long	inblocks, oublocks;
	long	msgsnd, msgrcv;
	long	nsignals;
	long	nvcsw, nivcsw;
	long	vsize, psize;
	/* fields accumulated from JOB_STEP records */
	int	error;

} jobs[MAX_JOBS];
long Njobs = 0;

struct {
	long	j;		/* index into jobs */
	long	jobstep;	/* job's step number */
	long	next;		/* linked list of job steps */
	char	finished[15];	/* YYYYMMDDhhmmss */
	char	cstatus[3];
	int	error;
	long	nprocs, ncpus;
	long	elapsed;
	long	tot_cpu_sec, tot_cpu_usec;
	long	tot_user_sec, tot_user_usec;
	long	tot_sys_sec, tot_sys_usec;
	long	rss, ixrss, idrss, isrss;
	long	minflt, majflt;
	long	nswap;
	long	inblocks, oublocks;
	long	msgsnd, msgrcv;
	long	nsignals;
	long	nvcsw, nivcsw;
	long	vsize, psize;
} jobsteps[MAX_JOBSTEPS];
long Njobsteps = 0;

int  cmp_long(const void *a1, const void *a2) {
	long *l1 = (long *) a1;
	long *l2 = (long *) a2;
	if (*l1 <  *l2)
		return -1;
	else if (*l1 == *l2)
		return 0;
	return 1;
}

char *decodeCstatus(char *cs)
{
	static char buf[10];

	if (strcasecmp(cs, "ca")==0) 
		return "CANCELLED";
	else if (strcasecmp(cs, "cd")==0) 
		return "COMPLETED";
	else if (strcasecmp(cs, "cg")==0) 
		return "COMPLETING";	/* we should never see this */
	else if (strcasecmp(cs, "f")==0) 
		return "FAILED";
	else if (strcasecmp(cs, "nf")==0)
		return "NODEFAILED";
	else if (strcasecmp(cs, "p")==0)
		return "PENDING"; 	/* we should never see this */
	else if (strcasecmp(cs, "r")==0)
		return "RUNNING"; 
	else if (strcasecmp(cs, "to")==0)
		return "TIMEDOUT";

	snprintf(buf, sizeof(buf),"CODE=%s", cs);
	return buf;
} 

void doDump(void)
{
	long	j, js;

	for (j=0; j<Njobs; j++) {
		if (opt_uid>=0)
			if (jobs[j].uid != opt_uid)
				continue;
		/* JOB_START */
		if (opt_jobstep_list == NULL) {
			if ((jobs[j].job_start_seen==0) &&
			    (jobs[j].job_step_seen) ) {
				/* If we only saw JOB_TERMINATED, the
				 * job was probably canceled. */ 
				fprintf(stderr,
					"Error: No JOB_START record for "
					"job %ld\n",
					jobs[j].job);
				if (rc<ERROR)
					rc = ERROR;
			}
			dumpHeader(j);
			printf("JOB_START %d %d %d %d %s %d %d %ld %s\n", 
				1,
				16,
				jobs[j].uid,
				jobs[j].gid,
				jobs[j].jobname,
				jobs[j].batch,
				jobs[j].priority,
				jobs[j].ncpus,
				jobs[j].nodes
		      );
		}
		/* JOB_STEP */
		for (js=jobs[j].first_jobstep; js>=0; js=jobsteps[js].next) {
			if ((strcasecmp(jobsteps[js].cstatus, "R")==0) &&
				jobs[j].job_terminated_seen) {
					strcpy(jobsteps[js].cstatus,"f");
					jobsteps[js].error=1;
			}
			dumpHeader(j);
			printf("JOB_STEP %d %d %ld ",
					1,
					38,
					jobsteps[js].jobstep
			      ); 
			printf("%s %s %d %ld %ld %ld ",
					jobsteps[js].finished,
					jobsteps[js].cstatus,
					jobsteps[js].error,
					jobsteps[js].nprocs,
					jobsteps[js].ncpus,
					jobsteps[js].elapsed
			);
			printf("%ld %ld %ld %ld %ld %ld ",
					jobsteps[js].tot_cpu_sec,
					jobsteps[js].tot_cpu_usec,
					jobsteps[js].tot_user_sec,
					jobsteps[js].tot_user_usec,
					jobsteps[js].tot_sys_sec,
					jobsteps[js].tot_sys_usec
			);
			printf("%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n",
					jobsteps[js].rss,
					jobsteps[js].ixrss,
					jobsteps[js].idrss,
					jobsteps[js].isrss,
					jobsteps[js].minflt,
					jobsteps[js].majflt,
					jobsteps[js].nswap,
					jobsteps[js].inblocks,
					jobsteps[js].oublocks,
					jobsteps[js].msgsnd,
					jobsteps[js].msgrcv,
					jobsteps[js].nsignals,
					jobsteps[js].nvcsw,
					jobsteps[js].nivcsw,
					jobsteps[js].vsize,
					jobsteps[js].psize
			      );
		}
		/* JOB_TERMINATED */
		if (opt_jobstep_list == NULL ) {
			dumpHeader(j);
			printf("JOB_TERMINATED %d %d %ld ",
				1,
				38,
				jobs[j].elapsed
		      	);
			printf("%s %s %d %ld %ld %ld ",
				jobs[j].finished,
				jobs[j].cstatus,
				jobs[j].error,
				jobs[j].nprocs,
				jobs[j].ncpus,
				jobs[j].elapsed
			);
			printf("%ld %ld %ld %ld %ld %ld ",
				jobs[j].tot_cpu_sec,
				jobs[j].tot_cpu_usec,
				jobs[j].tot_user_sec,
				jobs[j].tot_user_usec,
				jobs[j].tot_sys_sec,
				jobs[j].tot_sys_usec
			);
			printf("%ld %ld %ld %ld %ld %ld ",
				jobs[j].rss,
				jobs[j].ixrss,
				jobs[j].idrss,
				jobs[j].isrss,
				jobs[j].minflt,
				jobs[j].majflt
			      );
			printf("%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n", 
				jobs[j].nswap,
				jobs[j].inblocks,
				jobs[j].oublocks,
				jobs[j].msgsnd,
				jobs[j].msgrcv,
				jobs[j].nsignals,
				jobs[j].nvcsw,
				jobs[j].nivcsw,
				jobs[j].vsize,
				jobs[j].psize
		      );
		}
	}
}

/* doExpire() -- purge expired data from the accounting log file
 *
 * What we're doing:
 *  1. Open logfile.orig
 *  2. stat logfile.orig
 *     - confirm that it's not a sym link
 *     - capture the ownership and permissions
 *  3. scan logfile.orig for JOB_TERMINATED records with F_FINISHED dates
 *     that precede the specified expiration date. Build exp_table as
 *     a list of expired job numbers.
 *  4. Open logfile.expired for append
 *  5. Create logfile.new as ".new.<logfile>" (output with line buffering)
 *  6. Re-scan logfile.orig, writing
 *     - Expired job records to logfile.expired
 *     - Other job records to logfile.new
 *  7. Rename logfile.orig as ".old.<logfile>"
 *  8. Rename logfile.new as "<logfile>"
 *  9. Execute "scontrol reconfigure" which will cause slurmctld to
 *     start writing to logfile.new
 * 10. fseek(ftell(logfile.orig)) to clear EOF
 * 11. Copy any new records from logfile.orig to logfile.new
 * 12. Close logfile.expired, logfile.new
 * 13. Unlink .old.<logfile>
 */

void doExpire(void)
{
#define EXP_STG_LENGTH 12
#define TERM_REC_FIELDS 11
	char	buf[BUFSIZE],
		exp_stg[EXP_STG_LENGTH+1], /* YYYYMMDDhhmm */
		*expired_logfile_name,
		*f[TERM_REC_FIELDS],
		*fptr,
		*new_logfile_name,
		*old_logfile_name;
	int	new_file,
		i;
	long	*exp_table = NULL;	/* table of expired job numbers */
	int	exp_table_allocated = 0,
		exp_table_entries = 0;
	pid_t	pid;
	struct	stat statbuf;
	mode_t	prot = 0600;
	uid_t	uid;
	gid_t	gid;
	FILE	*expired_logfile,
		*new_logfile;

	/* Figure out our expiration date */
	{
		struct tm	*ts;	/* timestamp decoder */
		time_t		expiry;
		expiry = time(NULL)-opt_expire;
		ts = gmtime(&expiry);
		sprintf(exp_stg, "%04d%02d%02d%02d%02d",
			(ts->tm_year+1900), (ts->tm_mon+1), ts->tm_mday,
			ts->tm_hour, ts->tm_min);
		if (opt_debug)
			fprintf(stderr, "Purging jobs completed prior to %s\n",
					exp_stg);
	}
	/* Open the current or specified logfile, or quit */
	openLogFile();
	if (stat(opt_filein, &statbuf)) {
		perror("stat'ing logfile");
		exit(1);
	}
	if ((statbuf.st_mode & S_IFLNK) == S_IFLNK) {
		fprintf(stderr, "%s is a symbolic link; --expire requires "
				"a hard-linked file name\n", opt_filein);
		exit(1);
	}
	if ( (statbuf.st_mode & S_IFREG)==0 ) {
		fprintf(stderr, "%s is not a regular file; --expire "
				"only works on accounting log files\n",
				opt_filein);
		exit(1);
	}
	prot = statbuf.st_mode & 0777;
	gid  = statbuf.st_gid;
	uid  = statbuf.st_uid;
	old_logfile_name = prefix_filename(opt_filein, ".old.");
	if (stat(old_logfile_name, &statbuf)) {
		if (errno != ENOENT) {
			fprintf(stderr,"Error checking for %s: ",
					old_logfile_name);
			perror("");
			exit(1);
		}
	} else {
		fprintf(stderr, "Warning! %s exists -- please remove "
				"or rename it before proceeding",
				old_logfile_name);
		exit(1);
	}
	while (1) {
		if (fgets(buf, BUFSIZE, logfile) == NULL)
			break;
		lc++;
		fptr = buf;	/* break the record into NULL-
				   terminated strings */
		for (i = 0; i < TERM_REC_FIELDS; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL)
				break; 
			else
				*fptr++ = 0;
		}
		if (i < TERM_REC_FIELDS)
			continue;	/* Odd, but complain some other time */
		if (strcmp(f[F_RECVERSION], "1")) {
			fprintf(stderr,
				"Invalid record version \"%s\"at input "
				"line %ld\nIt is unsafe to complete this "
				"operation -- terminating\n",
				f[F_RECVERSION], lc);
			exit(1);
		}
		if (strcmp(f[F_RECTYPE], "JOB_TERMINATED")==0) {
			if (strncmp(exp_stg, f[F_FINISHED],
						expire_time_match)<0) 
				continue;
			if (NpartitionsSelected) {
				for (i = 0; i < NpartitionsSelected; i++)
					if (strcasecmp(f[F_PARTITION],
						partitionsSelected[i]) == 0)
						goto pmatch;
				continue;	/* no match */
			}
		    pmatch: 
			if (exp_table_allocated <= exp_table_entries)
				exp_table = _my_realloc( exp_table,
				    sizeof(long)*(exp_table_allocated+=10000));
			exp_table[exp_table_entries++] =
				strtol(f[F_JOB], NULL, 10);
		}
	}
	if (exp_table_entries == 0) {
		printf("No job records were purged.\n");
		exit(0);
	}
	expired_logfile_name = _my_malloc(strlen(opt_filein)+sizeof(".expired"));
	sprintf(expired_logfile_name, "%s.expired", opt_filein);
	new_file = stat(expired_logfile_name, &statbuf);
	if ((expired_logfile = fopen(expired_logfile_name, "a"))==NULL) {
		fprintf(stderr, "Error while opening %s", 
				expired_logfile_name);
		perror("");
		exit(1);
	}
	if (new_file) {  /* By default, the expired file looks like the log */
		chmod(expired_logfile_name, prot);
		chown(expired_logfile_name, uid, gid);
	}
	new_logfile_name = prefix_filename(opt_filein, ".new.");
	if ((new_logfile = fopen(new_logfile_name, "w"))==NULL) {
		fprintf(stderr, "Error while opening %s",
				new_logfile_name);
		perror("");
		exit(1);
	}
	chmod(new_logfile_name, prot);     /* preserve file protection */
	chown(new_logfile_name, uid, gid); /* and ownership */
		/* Use line buffering to allow us to safely write
		 * to the log file at the same time as slurmctld. */ 
	if (setvbuf(new_logfile, NULL, _IOLBF, 0)) {
		perror("setvbuf()");
		exit(1);
	}

	qsort(exp_table, exp_table_entries, sizeof(long), cmp_long);
	if (opt_debug) {
		fprintf(stderr, "--- debug: contents of exp_table ---");
		for (i=0; i<exp_table_entries; i++) {
			if ((i%5)==0)
				fprintf(stderr, "\n");
			else
				fprintf(stderr, "\t");
			fprintf(stderr, "%ld", exp_table[i]);
		}
		fprintf(stderr, "\n---- debug: end of exp_table ---\n");
	}
	rewind(logfile);
	while (fgets(buf, BUFSIZE, logfile)) {
		long tmp;
		tmp = strtol(buf, NULL, 10);
		if (bsearch(&tmp, exp_table, exp_table_entries,
					sizeof(long), cmp_long)) {
			if (fputs(buf, expired_logfile)<0) {
				perror("writing expired_logfile");
				exit(1);
			}
		} else {
			if (fputs(buf, new_logfile)<0) {
				perror("writing new_logfile");
				exit(1);
			}
		}
	}
	fclose(expired_logfile);
	if (rename(opt_filein, old_logfile_name)) {
		perror("renaming logfile to .old.");
		exit(1);
	}
	if (rename(new_logfile_name, opt_filein)) {
		perror("renaming new logfile");
		if (rename(old_logfile_name, opt_filein)==0) /* undo it? */
			fprintf(stderr, "Please correct the problem "
					"and try again");
		else
			fprintf(stderr, "SEVERE ERROR: Current accounting "
				"log may have been renamed %s;\n"
				"please rename it to \"%s\" if necessary, "
			        "and try again\n",
				old_logfile_name, opt_filein);
		exit(1);
	}
	fflush(new_logfile);	/* Flush the buffers before forking */
	fflush(logfile);
	if ((pid=fork())) {
		if (waitpid(pid, &i, 0) < 1) {
			perror("forking scontrol");
			exit(1);
		}
	} else {
		fclose(new_logfile);
		fclose(logfile);
		execlp("scontrol", "scontrol", "reconfigure", NULL);
		perror("attempting to run \"scontrol reconfigure\"");
		exit(1);
	}
	if (WEXITSTATUS(i)) {
		fprintf(stderr, "Error: Attempt to execute \"scontrol "
				"reconfigure\" failed. If SLURM is\n"
				"running, please rename the file \"%s\"\n"
				" to \"%s\" and try again.\n",
				old_logfile_name, opt_filein);
	}
	if (fseek(logfile, 0, SEEK_CUR)) {	/* clear EOF */
		perror("looking for late-arriving records");
		exit(1);
	}
	while (fgets(buf, BUFSIZE, logfile)) {
		if (fputs(buf, new_logfile)<0) {
			perror("writing final records");
			exit(1);
		}
	}
	fclose(new_logfile);
	fclose(logfile);
	unlink(old_logfile_name);
	printf("%d jobs purged.\n", exp_table_entries);
	exit(0);
}

void doFdump(char* fields[])
{
	int	i=0,
		numfields;
	struct {
		char	*start;
		char	*step;
		char	*term;
	} fnames[] = {
		{ "job",	"job",	"job"	},		/*  0 */
		{ "partition",	"partition",	"partition" },	/*  1 */
		{ "submitted",	"submitted",	"submitted" },	/*  2 */
		{ "starttime",	"starttime",	"starttime" },	/*  3 */
		{ "uid.gid",	"reserved-2",	"uid.gid" },	/*  4 */
		{ "reserved-1",	"reserved-1",	"reserved-1" },	/*  5 */
		{ "recordType", "recordType",	"recordType" },	/*  6 */
		{ "recordVers",	"recordVers",	"recordVers" },	/*  7 */
		{ "numFields",	"numFields",	"numFields" },	/*  8 */
		{ "uid",	"jobStep",	"totElapsed" },	/*  9 */
		{ "gid",	"finished",	"finished" },	/* 10 */
		{"jobName",	"cStatus",	"cStatus" },	/* 11 */ 
		{"batchFlag",	"error",	"error" },	/* 12 */
		{"priority",	"nprocs",	"nprocs" },	/* 13 */
		{"ncpus",	"ncpus",	"ncpus" },	/* 14 */
		{"nodeList",	"elapsed",	"elapsed" },	/* 15 */
		{"????",	"cpu_sec",	"cpu_sec" },	/* 16 */
		{"????",	"cpu_usec",	"cpu_usec" },	/* 17 */
		{"????",	"user_sec",	"user_sec" },	/* 18 */
		{"????",	"user_usec",	"user_usec" },	/* 19 */
		{"????",	"sys_sec",	"sys_sec" },	/* 20 */
		{"????",	"sys_usec",	"sys_usec" },	/* 21 */
		{"????",	"rss",		"rss" },	/* 22 */
		{"????",	"ixrss",	"ixrss" },	/* 23 */
		{"????",	"idrss",	"idrss" },	/* 24 */
		{"????",	"isrss",	"isrss" },	/* 25 */
		{"????",	"minflt",	"minflt" },	/* 26 */
		{"????",	"majflt",	"majflt" },	/* 27 */
		{"????",	"nswap",	"nswap" },	/* 28 */
		{"????",	"inblocks",	"inblocks" },	/* 29 */
		{"????",	"oublocks",	"oublocks" },	/* 30 */
		{"????",	"msgsnd",	"msgsnd" },	/* 31 */
		{"????",	"msgrcv",	"msgrcv" },	/* 32 */
		{"????",	"nsignals",	"nsignals" },	/* 33 */
		{"????",	"nvcsw",	"nvcsw" },	/* 34 */
		{"????",	"nivcsw",	"nivcsw" },	/* 35 */
		{"????",	"vsize",	"vsize" },	/* 36 */
		{"????",	"psize",	"psize" }	/* 37 */
	};
	numfields = atoi(fields[F_NUMFIELDS]);
	printf("\n-------Line %ld ---------------\n", lc);
	if (strcmp(fields[F_RECTYPE], "JOB_START")==0)
		for (i=0; fields[i] && (i<numfields); i++)
			printf("%12s: %s\n", fnames[i].start, fields[i]);
	else if (strcmp(fields[F_RECTYPE], "JOB_STEP")==0)
		for (i=0; fields[i] && (i<numfields); i++)
			printf("%12s: %s\n", fnames[i].step, fields[i]);
	else if (strcmp(fields[F_RECTYPE], "JOB_TERMINATED")==0)
		for (i=0; fields[i] && (i<numfields); i++)
			printf("%12s: %s\n", fnames[i].term, fields[i]);
	else 	/* getData() already told them of unknown record type */
		printf("      Field[%02d]: %s\n", i, fields[i]); 
	for ( ; fields[i]; i++)	/* Any extra data? */
		printf("extra field[%02d]: %s\n", i, fields[i]);
}

void doHelp(void)
{
	switch (opt_help) {
	case 1:
		helpMsg();
		break;
	case 2:
		helpFieldsMsg();
		break;
	case 3:
		usage();
		break;
	default:
		fprintf(stderr, "sacct bug: opt_help=%d\n", opt_help);
	}
}

/* doList() -- List the assembled data
 *
 * In:	Nothing explicit.
 * Out:	void.
 *
 * At this point, we have already selected the desired data,
 * so we just need to print it for the user.
 */
void doList(void)
{
	int	f, pf;
	long	j, js;
	int	doJobs=1,
		doJobsteps=1,
		i;

	if (opt_total)
		doJobsteps = 0;
	else if (opt_jobstep_list)
		doJobs = 0;
	for (j=0; j<Njobs; j++) {
		if ((jobs[j].job_start_seen==0) && jobs[j].job_step_seen) {
			/* If we only saw JOB_TERMINATED, the job was
			 * probably canceled. */
			fprintf(stderr,
				"Error: No JOB_START record for " "job %ld\n",
				jobs[j].job);
			if (rc<ERROR)
				rc = ERROR;
		}
		if (opt_verbose) {
			if (jobs[j].job_start_seen==0)
				fprintf(stderr,
					"Note: No JOB_START record for "
					"job %ld\n",
					jobs[j].job);
			if (jobs[j].job_step_seen==0)
				fprintf(stderr,
					"Note: No JOB_STEP record for "
					"job %ld\n",
					jobs[j].job);
			if (jobs[j].job_terminated_seen==0)
				fprintf(stderr,
					"Note: No JOB_TERMINATED record for "
					"job %ld\n",
					jobs[j].job);
		}
		if (opt_uid >= 0 && (jobs[j].uid != opt_uid))
			continue;
		if (opt_gid >= 0 && (jobs[j].gid != opt_gid))
			continue;
		if (doJobs) {
			if (opt_state_list) {
				for (i=0; i<NstatesSelected; i++) {
					if (strcasecmp(jobs[j].cstatus,
							statesSelected[i])==0)
						goto jstate;
				}
				continue;
			}
jstate:
			for (f=0; f<NprintFields; f++) {
				pf = printFields[f];
				if (f)
					printf(" ");
				(fields[pf].printRoutine)(JOB, j);
			}
			printf("\n");
		}
		if (doJobsteps) {
			for (js=jobs[j].first_jobstep;
					js>=0;
					js=jobsteps[js].next) {
				if ((strcasecmp(jobsteps[js].cstatus,"R")==0) &&
						jobs[j].job_terminated_seen) {
					strcpy(jobsteps[js].cstatus,"f");
					jobsteps[js].error=1;
				}
				if (opt_state_list) {
				    for (i=0; i<NstatesSelected; i++) {
					if (strcasecmp(jobsteps[js].cstatus,
							statesSelected[i])==0)
						goto js_state;
				    }
				    continue;
				}
js_state:
				for(f=0; f<NprintFields; f++) {
					pf = printFields[f];
					if (f)
						printf(" ");
					(fields[pf].printRoutine)(JOBSTEP, js);
				}
				printf("\n");
			} 
		}
	}
}

/* dumpHeader() -- dump the common fields of a record
 *
 * In:	Index into the jobs table
 * Out: Nothing.
 */
void dumpHeader(long j)
{
	printf("%ld %s %s %ld %s %s ",
			jobs[j].job,
			jobs[j].partition,
			jobs[j].submitted,
			jobs[j].starttime,
			"-",	/* reserved 2 */
			"-"	/* reserved 1 */
	      );
}

char *elapsedTime(long secs, long usecs)
{
	int	days, hours, minutes, seconds;
	char	daybuf[10],
		hourbuf[4],
		minbuf[4];
	static char	outbuf[20];  /* this holds LOTS of time! */
	div_t	res;

	daybuf[0] = 0;
	hourbuf[0] = 0;
	minbuf[0] = 0;

	res = div(usecs+5000, 1e6);	/* round up the usecs, then */
	usecs /= 1e4;			/* truncate to .00's */

	res = div(secs+res.quot, 60*60*24);	/* 1 day is 24 hours of 60
						   minutes of 60 seconds */
	days = res.quot;
	res = div(res.rem, 60*60);
	hours = res.quot;
	res = div(res.rem, 60);
	minutes = res.quot;
	seconds = res.rem;
	if (days) {
		snprintf(daybuf, sizeof(daybuf), "%d:", days);
		snprintf(hourbuf, sizeof(hourbuf), "%02d:", hours);
	} else if (hours)
		snprintf(hourbuf, sizeof(hourbuf), "%2d:", hours);
	if (days || hours)
		snprintf(minbuf, sizeof(minbuf), "%02d:", minutes);
	else if (minutes)
		snprintf(minbuf, sizeof(minbuf), "%2d:", minutes);
	if (days || hours || minutes)
		snprintf(outbuf, sizeof(outbuf), "%s%s%s%02d.%02ld",
			daybuf, hourbuf, minbuf, seconds, usecs);
	else
		snprintf(outbuf, sizeof(outbuf), "%2d.%02ld", seconds, usecs);
	return(outbuf);
}

int findJobRecord(long job)
{
	int	i;
	for (i=0; i<Njobs; i++) {
		if (jobs[i].job == job)
			return i;
	}
	return -1;
}

int findJobstepRecord(long j, long jobstep)
{
	int	i;
	if ((i=jobs[j].first_jobstep)<0)
		return -1;
	for (; i>=0; i=jobsteps[i].next) {
		if (jobsteps[i].jobstep == jobstep)
			return i;
	}
	return -1;
}

void getData(void)
{
	char	buf[BUFSIZE],
		*f[MAX_RECORD_FIELDS+1],	/* End list with null entry */
		*fptr;
	int	i;

	openLogFile();
	while (1) {
		if (fgets(buf, BUFSIZE, logfile) == NULL)
			break;
		lc++;
		fptr = buf;	/* break the record into NULL-
				   terminated strings */
		for (i = 0; i < MAX_RECORD_FIELDS; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL)
				break; 
			else
				*fptr++ = 0;
		}
		f[++i] = NULL;
		if (i < F_NUMFIELDS) {
			fprintf(stderr,
				"Invalid record (too short) "
				"at input line %ld\n", lc);
			if (opt_debug)
				fprintf(stderr, "rec> %s\n", buf);
			continue;
		}
		if (strcmp(f[F_RECVERSION], "1")) {
			fprintf(stderr,
				"Invalid record version at input line %ld\n",
				lc);
			inputError++;
			if (opt_debug)
				fprintf(stderr, "rec> %s\n", buf);
			continue;
		}
		if (NjobstepsSelected) {
			for (i = 0; i < NjobstepsSelected; i++) {
				if (strcmp(jobstepsSelected[i].job, f[F_JOB]))
					continue;
				/* job matches; does the step> */
				if (jobstepsSelected[i].step == NULL)
						goto foundjob;
				/* is it anything but JOB_STEP? */
				if (strcmp(f[F_RECTYPE], "JOB_STEP"))
						goto foundjob;
				if (strcmp(f[F_JOBSTEP],
						jobstepsSelected[i].step) == 0)
					goto foundjob;
			}
			continue;	/* no match */
		}
	      foundjob:
		if (NpartitionsSelected) {
			for (i = 0; i < NpartitionsSelected; i++)
				if (strcasecmp(f[F_PARTITION],
						partitionsSelected[i]) == 0)
					goto foundp;
			continue;	/* no match */
		}
	      foundp: 
		if (opt_fdump) {
			doFdump(f);
			continue;
		}
		/* Build suitable tables with all the data */
		if (strcmp(f[F_RECTYPE],"JOB_START")==0) {
			processJobStart(f);
		} else if (strcmp(f[F_RECTYPE],"JOB_STEP")==0) {
			processJobStep(f);
		} else if (strcmp(f[F_RECTYPE],"JOB_TERMINATED")==0) {
			processJobTerminated(f);
		} else {
			fprintf(stderr,
				"Invalid record at line %ld of input file\n",
				lc);
			inputError++;
			if (opt_debug)
				fprintf(stderr, "rec> %s\n", buf);
		}
	}
	if (ferror(logfile)) {
		perror(opt_filein);
		exit(1);
	}
	fclose(logfile);
	return;
}

void getOptions(int argc, char **argv)
{
	int c, i, optionIndex = 0;
	char *end, *start, *acct_type;
	struct stat stat_buf;
	static struct option long_options[] = {
		{"all", 0,0, 'a'},
		{"brief", 0, 0, 'b'},
		{"debug", 0, 0, 'D'},
		{"dump", 0, 0, 'd'},
		{"expire", 1, 0, 'e'},
		{"fields", 1, 0, 'F'},
		{"file", 1, 0, 'f'},
		{"formatted_dump", 0, 0, 'O'},
		{"gid", 1, 0, 'g'},
		{"group", 1, 0, 'g'},
		{"help", 0, &opt_help, 1},
		{"help-fields", 0, &opt_help, 2},
		{"jobs", 1, 0, 'j'},
		{"jobstep", 1, 0, 'J'},
		{"long", 0, 0, 'l'},
		{"big_logfile", 0, &opt_lowmem, 1},
		{"noheader", 0, &opt_header, 0},
		{"partition", 1, 0, 'p'},
		{"state", 1, 0, 's'},
		{"total", 0, 0,  't'},
		{"uid", 1, 0, 'u'},
		{"usage", 0, &opt_help, 3},
		{"user", 1, 0, 'u'},
		{"verbose", 0, 0, 'V'},
		{"version", 0, 0, 'v'},
		{0, 0, 0, 0}
	};

	if ((i=getuid()))	/* default to current user unless root*/
		opt_uid = i;

	opterr = 1;		/* Let getopt report problems to the user */

	while (1) {		/* now cycle through the command line */
		c = getopt_long(argc, argv, "abDde:F:f:g:hj:J:lOp:s:tUu:Vv",
				long_options, &optionIndex);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
			opt_uid = -1;
			break;
		case 'b':
			opt_field_list =
			    (char *) _my_realloc(opt_field_list,
						 (opt_field_list==NULL? 0 :
						 sizeof(opt_field_list)) +
						 sizeof(BRIEF_FIELDS)+1);
			strcat(opt_field_list, BRIEF_FIELDS);
			strcat(opt_field_list, ",");
			break;

		case 'D':
			opt_debug = 1;
			opt_verbose = 1;
			break;

		case 'd':
			opt_dump = 1;
			break;

		case 'e':
			{	/* decode the time spec */
			    long	acc=0;
			    int		i;
			    opt_expire_timespec = strdup(optarg);
			    for (i=0; opt_expire_timespec[i]; i++) {
				char	c = opt_expire_timespec[i];
				if (isdigit(c)) {
					acc = (acc*10)+(c-'0');
					continue;
				}
				switch (c) {
				    case 'D':
				    case 'd':
					opt_expire += acc*SECONDS_IN_DAY;
					expire_time_match = MATCH_DAY;
					acc=0;
					break;
				    case 'H':
				    case 'h':
					opt_expire += acc*SECONDS_IN_HOUR;
					expire_time_match = MATCH_HOUR;
					acc=0;
					break;
				    case 'M':
				    case 'm':
					opt_expire += acc*SECONDS_IN_MINUTE;
					expire_time_match = MATCH_MINUTE;
					acc=0;
					break;
				    default:
					opt_expire = -1;
					goto bad_timespec;
				} 
			    }
			    opt_expire += acc*SECONDS_IN_MINUTE;
			    if ((expire_time_match == 0) || (acc != 0))
				    /* If they say "1d2", we interpret that
				     * as 24 hours+2 minutes ago. */
				    expire_time_match = MATCH_MINUTE;
		bad_timespec:
			    if (opt_expire <= 0) {
				    fprintf(stderr,
					"Invalid timspec for "
						"--expire: \"%s\"\n",
					opt_expire_timespec);
				    exit(1);
			    }
			}
			opt_uid = -1;	/* fix default; can't purge by uid */
			break;

		case 'F':
			opt_field_list =
			    (char *) _my_realloc(opt_field_list,
						 (opt_field_list==NULL? 0 :
						 strlen(opt_field_list)) +
						 strlen(optarg) + 1);
			strcat(opt_field_list, optarg);
			strcat(opt_field_list, ",");
			break;

		case 'f':
			if (opt_debug)
				fprintf(stderr, "filein: %s\n", optarg);
			opt_filein =
			    (char *) _my_realloc(opt_filein, strlen(optarg)+1);
			strcpy(opt_filein, optarg);
			break;

		case 'g':
			if (isdigit((int) *optarg))
				opt_gid = atoi(optarg);
			else {
				struct group *grp;
				if ((grp=getgrnam(optarg))==NULL) {
					fprintf(stderr,
						"Invalid group id: %s\n",
						optarg);
					exit(1);
				}
				opt_gid=grp->gr_gid;
			}
			break;

		case 'h':
			opt_help = 1;
			break;

		case 'j':
			opt_job_list =
			    (char *) _my_realloc(opt_job_list,
						 (opt_job_list==NULL? 0 :
						 strlen(opt_job_list)) +
						 strlen(optarg) + 1);
			strcat(opt_job_list, optarg);
			strcat(opt_job_list, ",");
			break;

		case 'J':
			opt_jobstep_list =
			    (char *) _my_realloc(opt_jobstep_list,
						 (opt_jobstep_list==NULL? 0 :
						 strlen(opt_jobstep_list)) +
						 strlen(optarg) + 1);
			strcat(opt_jobstep_list, optarg);
			strcat(opt_jobstep_list, ",");
			break;

		case 'l':
			opt_field_list =
			    (char *) _my_realloc(opt_field_list,
						 (opt_field_list==NULL? 0 :
						 strlen(opt_field_list)) +
						 sizeof(LONG_FIELDS)+1);
			strcat(opt_field_list, LONG_FIELDS);
			strcat(opt_field_list, ",");
			break;

		case 'O':
			opt_fdump = 1;
			break;

		case 'p':
			opt_partition_list =
			    (char *) _my_realloc(opt_partition_list,
						 (opt_partition_list==NULL? 0 :
						 strlen(opt_partition_list)) +
						 strlen(optarg) + 1);
			strcat(opt_partition_list, optarg);
			strcat(opt_partition_list, ",");
			break;

		case 's':
			opt_state_list =
			    (char *) _my_realloc(opt_state_list,
						 (opt_state_list==NULL? 0 :
						 strlen(opt_state_list)) +
						 strlen(optarg) + 1);
			strcat(opt_state_list, optarg);
			strcat(opt_state_list, ",");
			break;

		case 't':
			opt_total = 1;
			break;

		case 'U':
			opt_help = 3;
			break;

		case 'u':
			if (isdigit((int) *optarg))
				opt_uid = atoi(optarg);
			else {
				struct passwd *pwd;
				if ((pwd=getpwnam(optarg))==NULL) {
					fprintf(stderr, "Invalid user id: %s\n",
							optarg);
					exit(1);
				}
				opt_uid=pwd->pw_uid;
			}
			break;

		case 'V':
			opt_verbose = 1;
			break;

		case 'v':
			{
				int	i;
				char	obuf[20]; /* should be long enough */
				char	*rev="$Revision$";
				char	*s;

				s=strstr(rev, " ")+1;
				for (i=0; s[i]!=' '; i++)
					obuf[i]=s[i];
				obuf[i] = 0;
				printf("%s: %s\n", argv[0], obuf);
				exit(0);
			}

		case ':':
		case '?':	/* getopt() has explained it */
			exit(1); 
		}
	}

	if (opt_debug) {
		fprintf(stderr, "Options selected:\n"
			"\topt_debug=%d\n"
			"\topt_dump=%d\n"
			"\topt_expire=%s (%lu seconds)\n"
			"\topt_fdump=%d\n"
			"\topt_field_list=%s\n"
			"\topt_filein=%s\n"
			"\topt_header=%d\n"
			"\topt_help=%d\n"
			"\topt_job_list=%s\n"
			"\topt_jobstep_list=%s\n"
			"\topt_long=%d\n"
			"\topt_lowmem=%d\n"
			"\topt_partition_list=%s\n"
			"\topt_state_list=%s\n"
			"\topt_total=%d\n"
			"\topt_verbose=%d\n",
			opt_debug,
			opt_dump,
			opt_expire_timespec, opt_expire,
			opt_fdump,
			opt_field_list,
			opt_filein,
			opt_header,
			opt_help,
			opt_job_list,
			opt_jobstep_list,
			opt_long,
			opt_lowmem,
			opt_partition_list,
			opt_state_list,
			opt_total,
			opt_verbose);
	}

	/* check if we have accounting data to view */
	if (opt_filein == NULL)
		opt_filein = slurm_get_jobacct_loc();
	acct_type = slurm_get_jobacct_type();
	if ((strcmp(acct_type, "jobacct/none") == 0)
	&&  (stat(opt_filein, &stat_buf) != 0)) {
		fprintf(stderr, "SLURM accounting is disabled\n");
		exit(1);
	}
	xfree(acct_type);

	/* specific partitions requested? */
	if (opt_partition_list) {

		start = opt_partition_list;
		while ((end = strstr(start, ","))) {
			*end = 0;;
			partitionsSelected[NpartitionsSelected] = start;
			NpartitionsSelected++;
			start = end + 1;
		}
		if (opt_debug) {
			int i;
			fprintf(stderr, "Partitions requested:\n");
			for (i = 0; i < NpartitionsSelected; i++)
				fprintf(stderr, "\t: %s\n",
					partitionsSelected[i]);
		}
	}

	/* specific jobsteps requested? */
	if (opt_jobstep_list) {
		char *dot;

		start = opt_jobstep_list;
		while ((end = strstr(start, ","))) {
			*end = 0;;
			dot = strstr(start, ".");
			if (dot == NULL) {
				fprintf(stderr, "Invalid jobstep: %s\n",
					start);
				exit(1);
			}
			*dot++ = 0;
			jobstepsSelected[NjobstepsSelected].job = start;
			jobstepsSelected[NjobstepsSelected].step = dot;
			NjobstepsSelected++;
			start = end + 1;
		}
		if (opt_debug) {
			int i;
			fprintf(stderr, "Job steps requested:\n");
			for (i = 0; i < NjobstepsSelected; i++)
				fprintf(stderr, "\t: %s.%s\n",
					jobstepsSelected[i].job,
					jobstepsSelected[i].step);
		}
	}

	/* specific jobs requested? */
	if (opt_job_list) { 
		start = opt_job_list;
		while ((end = strstr(start, ","))) {
			*end = 0;
			jobstepsSelected[NjobstepsSelected].job = start;
			jobstepsSelected[NjobstepsSelected].step = NULL;
			NjobstepsSelected++;
			start = end + 1;
		}
		if (opt_debug) {
			int i;
			fprintf(stderr, "Jobs requested:\n");
			for (i = 0; i < NjobstepsSelected; i++)
				fprintf(stderr, "\t: %s\n",
					jobstepsSelected[i].job);
		}
	}

	/* specific states (completion status) requested? */
	if (opt_state_list) {
		start = opt_state_list;
		while ((end = strstr(start, ","))) {
			*end = 0;
			statesSelected[NstatesSelected] = start;
			NstatesSelected++;
			start = end + 1;
		}
		if (opt_debug) {
			int i;
			fprintf(stderr, "States requested:\n");
			for (i = 0; i < NstatesSelected; i++)
				fprintf(stderr, "\t: %s\n", statesSelected[i]);
		}
	}

	/* select the output fields */
	if (opt_field_list==NULL) {
		if (opt_dump || opt_expire)
			return;
		opt_field_list = _my_malloc(sizeof(DEFAULT_FIELDS)+1);
		strcpy(opt_field_list,DEFAULT_FIELDS); 
		strcat(opt_field_list, ",");
	}
	start = opt_field_list;
	while ((end = strstr(start, ","))) {
		*end = 0;
		for (i = 0; fields[i].name; i++) {
			if (strcasecmp(fields[i].name, start) == 0)
				goto foundfield;
		}
		fprintf(stderr,
			"Invalid field requested: \"%s\"\n",
			start);
		exit(1);
	      foundfield:
		printFields[NprintFields++] = i;
		start = end + 1;
	}
	if (opt_debug) {
		fprintf(stderr, "%d field%s selected:\n",
				NprintFields,
				(NprintFields==1? "" : "s"));
		for (i = 0; i < NprintFields; i++)
			fprintf(stderr,
				"\t%s\n",
				fields[printFields[i]].name);
	}
	return;
}
void helpFieldsMsg(void)
{
	int i;

	for (i = 0; fields[i].name; i++) {
		if (i & 3)
			printf("  ");
		else
			printf("\n");
		printf("%-10s", fields[i].name);
	}
	printf("\n");
	return;
}

void helpMsg(void)
{
	printf("\n"
	       "By default, sacct displays accounting data for all jobs and job\n"
	       "steps that are present in the log.\n"
	       "\n"
	       "Notes:\n"
	       "\n"
	       "    * If --dump is specified,\n"
	       "          * The field selection options (--brief, --fields, ...)\n"
	       "	    have no effect\n"
	       "	  * Elapsed time fields are presented as 2 fields, integral\n"
	       "	    seconds and integral microseconds\n"
	       "    * If --dump is not specified, elapsed time fields are presented\n"
	       "      as [[days:]hours:]minutes:seconds.hundredths\n"
	       "    * The default input file is the file named in /etc/slurm.conf\n"
	       "      or /hptc_cluster/slurm/etc/slurm.conf. If no slurm.conf file\n"
	       "      is found, try to use /var/log/slurm_accounting.log.\n"
	       "\n"
	       "Options:\n"
	       "\n"
	       "-a, --all\n"
	       "    Display job accounting data for all users. By default, only\n"
	       "    data for the current user is displayed for users other than\n"
	       "    root.\n"
	       "-b, --brief\n"
	       "    Equivalent to \"--fields=jobstep,status,error\". This option\n"
	       "    has no effect if --dump is specified.\n"
	       "-d, --dump\n"
	       "    Dump the raw data records\n"
	       "-e <timespec>, --expire=<timespec>\n"
	       "    Remove jobs from SLURM's current accounting log file (or the\n"
	       "    file specified with --file) that completed more than <timespec>\n"
	       "    ago.  If <timespec> is an integer, it is interpreted as\n" 
	       "    minutes. If <timespec> is an integer followed by \"h\", it is\n"
	       "    interpreted as a number of hours. If <timespec> is an integer\n"
	       "    followed by \"d\", it is interpreted as number of days. For\n"
	       "    example, \"--expire=14d\" means that you wish to purge the job\n"
	       "    accounting log of all jobs that completed more than 14 days ago.\n" 
	       "-F <field-list>, --fields=<field-list>\n"
	       "    Display the specified data (use \"--help-fields\" for a\n"
	       "    list of available fields). If no field option is specified,\n"
	       "    we use \"--fields=jobstep,jobname,partition,ncpus,status,error\".\n"
	       "-f<file>, --file=<file>\n"
	       "    Read data from the specified file, rather than SLURM's current\n"
	       "    accounting log file.\n"
	       "-l, --long\n"
	       "    Equivalent to specifying\n"
	       "    \"--fields=jobstep,usercpu,systemcpu,minflt,majflt,nprocs,\n"
	       "    ncpus,elapsed,status,error\"\n"
	       "-O, --formatted_dump\n"
	       "    Dump accounting records in an easy-to-read format, primarily\n"
	       "    for debugging.\n"
	       "-g <gid>, --gid <gid>\n"
	       "    Select only jobs submitted from the <gid> group.\n"
	       "-h, --help\n"
	       "    Print a general help message.\n"
	       "--help-fields\n"
	       "    Print a list of fields that can be specified with the\n"
	       "    \"--fields\" option\n"
	       "-j <job_list>, --jobs=<job_list>\n"
	       "    Display information about this job or comma-separated\n"
	       "    list of jobs. The default is all jobs.\n"
	       "-J <job.step>, --jobstep=<job.step>\n"
	       "    Show data only for the specified step of the specified job\n"
	       "--noheader\n"
	       "    Print (or don't print) a header. The default is to print a\n"
	       "    header; the option has no effect if --dump is specified\n"
	       "-p <part_list>, --partition=<part_list>\n"
	       "    Display or purge information about jobs and job steps in the\n"
	       "    <part_list> partition(s). The default is all partitions.\n"
	       "-s <state-list>, --state=<state-list>\n"
	       "    Select jobs based on their current status: running (r),\n"
	       "    completed (cd), failed (f), timeout (to), and node_fail (nf).\n"
	       "-t, --total\n"
	       "    Only show cumulative statistics for each job, not the\n"
	       "    intermediate steps\n"
	       "-u <uid>, --uid <uid>\n"
	       "    Select only jobs submitted by the user with uid <uid>.  Only\n"
	       "    root users are allowed to specify a uid other than their own.\n"
	       "--usage\n"
	       "    Pointer to this message.\n"
	       "-V, --verbose\n"
	       "    Primarily for debugging purposes, report the state of various\n"
	       "    variables during processing.\n");
	return;
}

int main(int argc, char **argv)
{
	enum {
		DUMP,
		EXPIRE,
		FDUMP,
		LIST,
		HELP,
		USAGE
	} op;

	getOptions(argc, argv);

	/* What are we doing? Requests for help take highest priority,
	 * but then check for illogical switch combinations.
	 */

	if (opt_help)
		op = HELP;
	else if (opt_dump) {
		op = DUMP;
		if (opt_long || opt_total || opt_field_list || opt_expire) {
			if (opt_verbose)
				fprintf(stderr,
					"Switch conflict,\n"
					"\topt_long=%d\n"
					"\topt_total=%d\n"
					"\topt_field_list=%s\n",
					opt_long, opt_total, opt_field_list);
			invalidSwitchCombo("--dump",
				   "--brief, --long, --fields, --total");
			exit(1);
		}
	} else if (opt_fdump) {
		op = FDUMP;
	} else if (opt_expire) {
		op = EXPIRE;
		if (opt_long || opt_total || opt_field_list || 
				(opt_gid>=0) || (opt_uid>=0) ||
				opt_job_list || opt_jobstep_list ||
				opt_state_list ) {
			if (opt_verbose)
				fprintf(stderr,
					"Switch conflict,\n"
					"\topt_long=%d\n"
					"\topt_total=%d\n"
					"\topt_field_list=%s\n"
					"\topt_gid=%d\n"
					"\topt_uid=%d\n"
					"\topt_job_list=%s\n"
					"\topt_jobstep_list=%s\n"
					"\topt_state_list=%s\n",
					opt_long, opt_total, opt_field_list,
					opt_gid, opt_uid, opt_job_list,
					opt_jobstep_list, opt_state_list);
			invalidSwitchCombo("--expire",
				   "--brief, --long, --fields, --total, "
				   "--gid, --uid, --jobs, --jobstep,"
				   "--state");
			exit(1);
		}
	} else
		op = LIST;

	switch (op) {
	case DUMP:
		getData();
		doDump();
		break;
	case EXPIRE:
		doExpire();
		break;
	case FDUMP:
		getData();
		break;
	case LIST:
		if (opt_header)		/* give them something to look */
			printHeader();	/* at while we think...        */
		getData();
		doList();
		break;
	case HELP:
		doHelp();
		break;
	default:
		fprintf(stderr, "sacct bug: should never get here\n");
		exit(2);
	}
	return (rc);
}

long initJobStruct(long job, char *f[])
{
	long	j;
	char	*p;

	if ((j=Njobs++)>= MAX_JOBS) {
		fprintf(stderr, "Too many jobs, %ld, listed in log file\n", j);
		exit(2);
	} 
	jobs[j].job = job;
	jobs[j].job_start_seen = 0;
	jobs[j].job_step_seen = 0;
	jobs[j].job_terminated_seen = 0;
	jobs[j].first_jobstep = -1;
	jobs[j].partition = _my_malloc(strlen(f[F_PARTITION])+1);
	strcpy(jobs[j].partition, f[F_PARTITION]);
	strncpy(jobs[j].submitted, f[F_SUBMITTED], 16);
	strncpy(jobs[j].jobname, "(unknown)", MAX_JOBNAME_LENGTH);
	strcpy(jobs[j].cstatus,"r");
	strcpy(jobs[j].finished,"?");
	jobs[j].starttime = strtol(f[F_STARTTIME], NULL, 10);
	/* Early versions of jobacct treated F_UIDGID as a reserved
	 * field, so we might find "-" here. Take advantage of the
	 * fact that atoi() will return 0 if it finds something that's
	 * not a number for uid and gid.
	 */
	jobs[j].uid = atoi(f[F_UIDGID]);
	if ((p=strstr(f[F_UIDGID],".")))
		jobs[j].gid=atoi(++p);
	jobs[j].tot_cpu_sec = 0;
	jobs[j].tot_cpu_usec = 0;
	jobs[j].tot_user_sec = 0;
	jobs[j].tot_user_usec = 0;
	jobs[j].tot_sys_sec = 0;
	jobs[j].tot_sys_usec = 0;
	jobs[j].rss = 0;
	jobs[j].ixrss = 0;
	jobs[j].idrss = 0;
	jobs[j].isrss = 0;
	jobs[j].minflt = 0;
	jobs[j].majflt = 0;
	jobs[j].nswap = 0;
	jobs[j].inblocks = 0;
	jobs[j].oublocks = 0;
	jobs[j].msgsnd = 0;
	jobs[j].msgrcv = 0;
	jobs[j].nsignals = 0;
	jobs[j].nvcsw = 0;
	jobs[j].nivcsw = 0;
	jobs[j].vsize = 0;
	jobs[j].psize = 0;
	jobs[j].error = 0;
	return j;
}

void invalidSwitchCombo(char *good, char *bad)
{
	fprintf(stderr, "\"%s\" may not be used with %s\n", good, bad);
	return;
}

void linkJobstep(long j, long js)
{
	long	*current;
	current = &jobs[j].first_jobstep;
	while ( (*current>=0) &&
			(jobsteps[*current].jobstep < jobsteps[js].jobstep)) {
		current = &jobsteps[*current].next;
	}
	jobsteps[js].next = *current;
	*current = js; 
}

/* openLogFile() -- find the current or specified log file, and open it
 *
 * IN:		Nothing
 * RETURNS:	Nothing
 *
 * Side effects:
 * 	- Sets opt_filein to the current system accounting log unless
 * 	  the user specified another file.
 */

void openLogFile(void)
{
	logfile = fopen(opt_filein, "r");
	if (logfile == NULL) {
		perror(opt_filein);
		exit(1);
	}
	return;
}


/* Field-specific print routines */

void printCpu(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%15s", "Cpu");
			break;
		case UNDERSCORE:
			printf("%15s", "---------------");
			break;
		case JOB:
			printf("%15s",
				elapsedTime(jobs[idx].tot_cpu_sec,
				jobs[idx].tot_cpu_usec));
			break;
		case JOBSTEP:
			printf("%15s",
				elapsedTime(jobsteps[idx].tot_cpu_sec,
				jobsteps[idx].tot_cpu_usec));
			break;
	} 
}

void printElapsed(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%15s", "Elapsed");
			break;
		case UNDERSCORE:
			printf("%15s", "---------------");
			break;
		case JOB:
			printf("%15s", elapsedTime(jobs[idx].elapsed,0));
			break;
		case JOBSTEP:
			printf("%15s", elapsedTime(jobsteps[idx].elapsed,0));
			break;
	} 
}

void printError(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%5s", "Error");
			break;
		case UNDERSCORE:
			printf("%5s", "-----");
			break;
		case JOB:
			printf("%5d", jobs[idx].error);
			break;
		case JOBSTEP:
			printf("%5d", jobsteps[idx].error);
			break;
	} 
}

void printFinished(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%-14s", "Finished");
			break;
		case UNDERSCORE:
			printf("%-14s", "--------------");
			break;
		case JOB:
			printf("%-14s", jobs[idx].finished);
			break;
		case JOBSTEP:
			printf("%-14s", jobsteps[idx].finished);
			break;
	} 
}

void printGid(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%5s", "Gid");
			break;
		case UNDERSCORE:
			printf("%5s", "-----");
			break;
		case JOB:
			printf("%5d", jobs[idx].gid);
			break;
		case JOBSTEP:
			printf("%5d", jobs[jobsteps[idx].j].gid);
			break;
	} 
}

void printGroup(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%-9s", "Group");
			break;
		case UNDERSCORE:
			printf("%-9s", "---------");
			break;
		case JOB:
		case JOBSTEP:
			{
				char	*tmp="(unknown)";
				struct	group *gr;
				if ((gr=getgrgid(jobs[idx].gid)))
					tmp=gr->gr_name;
				printf("%-9s", tmp);
			}
			break;
	} 
}

void printIdrss(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%8s", "Idrss");
			break;
		case UNDERSCORE:
			printf("%8s", "------");
			break;
		case JOB:
			printf("%8ld", jobs[idx].idrss);
			break;
		case JOBSTEP:
			printf("%8ld", jobsteps[idx].idrss);
			break;
	} 
}

void printInblocks(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%9s", "Inblocks");
			break;
		case UNDERSCORE:
			printf("%9s", "---------");
			break;
		case JOB:
			printf("%9ld", jobs[idx].inblocks);
			break;
		case JOBSTEP:
			printf("%9ld", jobsteps[idx].inblocks);
			break;
	} 
}

void printIsrss(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%8s", "Isrss");
			break;
		case UNDERSCORE:
			printf("%8s", "------");
			break;
		case JOB:
			printf("%8ld", jobs[idx].isrss);
			break;
		case JOBSTEP:
			printf("%8ld", jobsteps[idx].isrss);
			break;
	} 

}

void printIxrss(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%8s", "Ixrss");
			break;
		case UNDERSCORE:
			printf("%8s", "------");
			break;
		case JOB:
			printf("%8ld", jobs[idx].ixrss);
			break;
		case JOBSTEP:
			printf("%8ld", jobsteps[idx].ixrss);
			break;
	} 

}

void printJob(print_what_t which, long idx)
{
	char	outbuf[12];
	switch(which) {
		case HEADLINE:
			printf("%-8s", "Job");
			break;
		case UNDERSCORE:
			printf("%-8s", "--------");
			break;
		case JOB:
			snprintf(outbuf, sizeof(outbuf), "%ld",
					jobs[idx].job);
			printf("%-8s", outbuf);
			break;
		case JOBSTEP:
			snprintf(outbuf, sizeof(outbuf), "%ld",
					jobs[jobsteps[idx].j].job);
			printf("%-8s", outbuf);
			break;
	} 
}

void printJobName(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%-18s", "Jobname");
			break;
		case UNDERSCORE:
			printf("%-18s", "------------------");
			break;
		case JOB:
			printf("%-18s", jobs[idx].jobname);
			break;
		case JOBSTEP:
			printf("%-18s", jobs[jobsteps[idx].j].jobname);
			break;
	} 
}

void printJobStep(print_what_t which, long idx)
{
	char	outbuf[12];
	switch(which) {
		case HEADLINE:
			printf("%-10s", "Jobstep");
			break;
		case UNDERSCORE:
			printf("%-10s", "----------");
			break;
		case JOB:
			snprintf(outbuf, sizeof(outbuf), "%ld",
					jobs[idx].job);
			printf("%-10s", outbuf);
			break;
		case JOBSTEP:
			snprintf(outbuf, sizeof(outbuf), "%ld.%ld",
					jobs[jobsteps[idx].j].job,
					jobsteps[idx].jobstep);
			printf("%-10s", outbuf);
			break;
	} 

}

void printMajFlt(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%8s", "Majflt");
			break;
		case UNDERSCORE:
			printf("%8s", "------");
			break;
		case JOB:
			printf("%8ld", jobs[idx].majflt);
			break;
		case JOBSTEP:
			printf("%8ld", jobsteps[idx].majflt);
			break;
	} 
}

void printMinFlt(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%8s", "Minflt");
			break;
		case UNDERSCORE:
			printf("%8s", "------");
			break;
		case JOB:
			printf("%8ld", jobs[idx].minflt);
			break;
		case JOBSTEP:
			printf("%8ld", jobsteps[idx].minflt);
			break;
	} 
}

void printMsgRcv(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%9s", "Msgrcv");
			break;
		case UNDERSCORE:
			printf("%9s", "---------");
			break;
		case JOB:
			printf("%9ld", jobs[idx].msgrcv);
			break;
		case JOBSTEP:
			printf("%9ld", jobsteps[idx].msgrcv);
			break;
	} 
}

void printMsgSnd(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%9s", "Msgsnd");
			break;
		case UNDERSCORE:
			printf("%9s", "---------");
			break;
		case JOB:
			printf("%9ld", jobs[idx].msgsnd);
			break;
		case JOBSTEP:
			printf("%9ld", jobsteps[idx].msgsnd);
			break;
	} 
}

void printNcpus(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%7s", "Ncpus");
			break;
		case UNDERSCORE:
			printf("%7s", "-------");
			break;
		case JOB:
			printf("%7ld", jobs[idx].ncpus);
			break;
		case JOBSTEP:
			printf("%7ld", jobsteps[idx].ncpus);
			break;
	} 
}

void printNivcsw(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%9s", "Nivcsw");
			break;
		case UNDERSCORE:
			printf("%9s", "---------");
			break;
		case JOB:
			printf("%9ld", jobs[idx].nivcsw);
			break;
		case JOBSTEP:
			printf("%9ld", jobsteps[idx].nivcsw);
			break;
	} 
}

void printNodes(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%-30s", "Nodes");
			break;
		case UNDERSCORE:
			printf("%-30s", "------------------------------");
			break;
		case JOB:
			printf("%-30s", jobs[idx].nodes);
			break;
		case JOBSTEP:
			printf("%-30s", "                              ");
			break;
	} 
}

void printNsignals(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%9s", "Nsignals");
			break;
		case UNDERSCORE:
			printf("%9s", "---------");
			break;
		case JOB:
			printf("%9ld", jobs[idx].nsignals);
			break;
		case JOBSTEP:
			printf("%9ld", jobsteps[idx].nsignals);
			break;
	} 
}

void printNswap(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%8s", "Nswap");
			break;
		case UNDERSCORE:
			printf("%8s", "------");
			break;
		case JOB:
			printf("%8ld", jobs[idx].nswap);
			break;
		case JOBSTEP:
			printf("%8ld", jobsteps[idx].nswap);
			break;
	} 
}

void printNprocs(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%7s", "Nprocs");
			break;
		case UNDERSCORE:
			printf("%7s", "-------");
			break;
		case JOB:
			printf("%7ld", jobs[idx].nprocs);
			break;
		case JOBSTEP:
			printf("%7ld", jobsteps[idx].nprocs);
			break;
	} 
}

void printNvcsw(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%9s", "Nvcsw");
			break;
		case UNDERSCORE:
			printf("%9s", "---------");
			break;
		case JOB:
			printf("%9ld", jobs[idx].nvcsw);
			break;
		case JOBSTEP:
			printf("%9ld", jobsteps[idx].nvcsw);
			break;
	} 
}

void printOutBlocks(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%9s", "Outblocks");
			break;
		case UNDERSCORE:
			printf("%9s", "---------");
			break;
		case JOB:
			printf("%9ld", jobs[idx].oublocks);
			break;
		case JOBSTEP:
			printf("%9ld", jobsteps[idx].oublocks);
			break;
	} 
}

void printPartition(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%-10s", "Partition");
			break;
		case UNDERSCORE:
			printf("%-10s", "----------");
			break;
		case JOB:
			printf("%-10s", jobs[idx].partition);
			break;
		case JOBSTEP:
			printf("%-10s", jobs[jobsteps[idx].j].partition);
			break;
	} 
}

void printPsize(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%10s", "Psize");
			break;
		case UNDERSCORE:
			printf("%10s", "------");
			break;
		case JOB:
			printf("%10ld", jobs[idx].psize);
			break;
		case JOBSTEP:
			printf("%10ld", jobsteps[idx].psize);
			break;
	} 
}

void printRss(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%8s", "Rss");
			break;
		case UNDERSCORE:
			printf("%8s", "------");
			break;
		case JOB:
			printf("%8ld", jobs[idx].rss);
			break;
		case JOBSTEP:
			printf("%8ld", jobsteps[idx].rss);
			break;
	} 
}

void printStatus(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%-10s", "Status");
			break;
		case UNDERSCORE:
			printf("%-10s", "----------");
			break;
		case JOB:
			printf("%-10s", decodeCstatus(jobs[idx].cstatus));
			break;
		case JOBSTEP:
			printf("%-10s", decodeCstatus(jobsteps[idx].cstatus));
			break;
	} 
}

void printSubmitted(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%-14s", "Submitted");
			break;
		case UNDERSCORE:
			printf("%-14s", "--------------");
			break;
		case JOB:
			printf("%-14s", jobs[idx].submitted);
			break;
		case JOBSTEP:
			printf("%-14s", jobs[jobsteps[idx].j].submitted);
			break;
	} 
}

void printSystemCpu(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%15s", "SystemCpu");
			break;
		case UNDERSCORE:
			printf("%15s", "---------------");
			break;
		case JOB:
			printf("%15s",
				elapsedTime(jobs[idx].tot_sys_sec,
				jobs[idx].tot_sys_usec));
			break;
		case JOBSTEP:
			printf("%15s",
				elapsedTime(jobsteps[idx].tot_sys_sec,
				jobsteps[idx].tot_sys_usec));
			break;
	} 

}

void printUid(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%5s", "Uid");
			break;
		case UNDERSCORE:
			printf("%5s", "-----");
			break;
		case JOB:
			printf("%5d", jobs[idx].uid);
			break;
		case JOBSTEP:
			printf("%5d", jobs[jobsteps[idx].j].uid);
			break;
	} 
}

void printUser(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%-9s", "User");
			break;
		case UNDERSCORE:
			printf("%-9s", "---------");
			break;
		case JOB:
		case JOBSTEP:
			{
				char	*tmp="(unknown)";
				struct	passwd *pw;
				if ((pw=getpwuid(jobs[idx].uid)))
					tmp=pw->pw_name;
				printf("%-9s", tmp);
			}
			break;
	} 
}

void printUserCpu(print_what_t which, long idx)
{
	switch(which) {
		case HEADLINE:
			printf("%15s", "UserCpu");
			break;
		case UNDERSCORE:
			printf("%15s", "---------------");
			break;
		case JOB:
			printf("%15s",
				elapsedTime(jobs[idx].tot_user_sec,
				jobs[idx].tot_user_usec));
			break;
		case JOBSTEP:
			printf("%15s",
				elapsedTime(jobsteps[idx].tot_user_sec,
				jobsteps[idx].tot_user_usec));
			break;
	} 

}

void printVsize(print_what_t which, long idx)
{ 
	switch(which) {
		case HEADLINE:
			printf("%10s", "Vsize");
			break;
		case UNDERSCORE:
			printf("%10s", "------");
			break;
		case JOB:
			printf("%10ld", jobs[idx].vsize);
			break;
		case JOBSTEP:
			printf("%10ld", jobsteps[idx].vsize);
			break;
	} 
}

/* prefix_filename() -- insert a filename prefix into a path
 *
 * IN:	path = fully-qualified path+file name
 *      prefix = the prefix to insert into the file name
 * RETURNS: pointer to the updated path+file name
 */

char *prefix_filename(char *path, char *prefix) {
	char	*out;
	int     i,
	plen;

	plen = strlen(path);
	out = _my_malloc(plen+strlen(prefix)+1);
	for (i=plen-1; i>=0; i--)
		if (path[i]=='/') {
			break;
		}
	i++;
	*out = 0;
	strncpy(out, path, i);
	out[i] = 0;
	strcat(out, prefix);
	strcat(out, path+i);
	return(out);
}

void printHeader(void)
{
	int	i,j;
	for (i=0; i<NprintFields; i++) {
		if (i)
			printf(" ");
		j=printFields[i];
		(fields[j].printRoutine)(HEADLINE, 0);
	}
	printf("\n");
	for (i=0; i<NprintFields; i++) {
		if (i)
			printf(" ");
		j=printFields[i];
		(fields[j].printRoutine)(UNDERSCORE, 0);
	}
	printf("\n");
}

void processJobStart(char *f[])
{
	int	i;
	long	j;	/* index in jobs[] */
	long	job;

	job = strtol(f[F_JOB], NULL, 10);
	j = findJobRecord(job);
	if (j >= 0 ) {	/* Hmmm... that's odd */
		if (jobs[j].job_start_seen) {
			fprintf(stderr,
				"Duplicate JOB_START for job %ld at line "
				"%ld -- ignoring it\n",
				job, lc);
			return;
		} /* Data out of order; we'll go ahead and populate it now */
	} else
		j = initJobStruct(job,f);
	jobs[j].job_start_seen = 1;
	jobs[j].uid = atoi(f[F_UID]);
	jobs[j].gid = atoi(f[F_GID]);
	strncpy(jobs[j].jobname, f[F_JOBNAME], MAX_JOBNAME_LENGTH);
	jobs[j].batch = atoi(f[F_BATCH]);
	jobs[j].priority = atoi(f[F_PRIORITY]);
	jobs[j].ncpus = strtol(f[F_NCPUS], NULL, 10);
	jobs[j].nodes = _my_malloc(strlen(f[F_NODES])+1);
	strcpy(jobs[j].nodes, f[F_NODES]); 
	for (i=0; jobs[j].nodes[i]; i++)	/* discard trailing <CR> */
		if (isspace(jobs[j].nodes[i]))
			jobs[j].nodes[i] = 0;
	if (strcmp(jobs[j].nodes, "(null)")==0) {
		free(jobs[j].nodes);
		jobs[j].nodes = "unknown";
	}
}

void processJobStep(char *f[])
{
	long	j,	/* index into jobs */
		js,	/* index into jobsteps */
		job,
		jobstep;

	job = strtol(f[F_JOB], NULL, 10);
	if (strcmp(f[F_JOBSTEP],NOT_JOBSTEP)==0)
		jobstep = -2;
	else
		jobstep = strtol(f[F_JOBSTEP], NULL, 10);
	j = findJobRecord(job);
	if (j<0) {	/* fake it for now */
		j = initJobStruct(job,f);
		if (opt_verbose && (opt_jobstep_list==NULL)) 
			fprintf(stderr, "Note: JOB_STEP record %ld.%ld preceded"
					" JOB_START record at line %ld\n",
					job, jobstep, lc);
	}
	if ((js=findJobstepRecord(j, jobstep))>=0) {
		if (strcasecmp(f[F_CSTATUS], "R")==0)
			return; /* if "R" record preceded by F or CD; unusual */
		if (strcasecmp(jobsteps[js].cstatus, "R")) { /* if not "R" */
			fprintf(stderr,
				"Duplicate JOB_STEP record for jobstep %ld.%ld "
				"at line %ld -- ignoring it\n",
				job, jobstep, lc);
			return;
		}
		goto replace_js;
	}
	if ((js = Njobsteps++)>=MAX_JOBSTEPS) {
		fprintf(stderr, "Too many jobsteps, %ld, in the log file\n", js);
		exit(2);
	}
	jobsteps[js].j = j;
	jobsteps[js].jobstep = jobstep;
	if (jobstep >= 0) {
		linkJobstep(j, js);
		jobs[j].job_step_seen = 1;
	}
  replace_js:
	strcpy(jobsteps[js].finished, f[F_FINISHED]);
	strcpy(jobsteps[js].cstatus, f[F_CSTATUS]);
	jobsteps[js].error = strtol(f[F_ERROR], NULL, 10);
	jobsteps[js].nprocs = strtol(f[F_NPROCS], NULL, 10);
	jobsteps[js].ncpus = strtol(f[F_NCPUS], NULL, 10);
	jobsteps[js].elapsed = strtol(f[F_ELAPSED], NULL, 10);
	jobsteps[js].tot_cpu_sec = strtol(f[F_CPU_SEC], NULL, 10);
	jobsteps[js].tot_cpu_usec = strtol(f[F_CPU_USEC], NULL, 10);
	jobsteps[js].tot_user_sec = strtol(f[F_USER_SEC], NULL, 10);
	jobsteps[js].tot_user_usec = strtol(f[F_USER_USEC], NULL, 10);
	jobsteps[js].tot_sys_sec = strtol(f[F_SYS_SEC], NULL, 10);
	jobsteps[js].tot_sys_usec = strtol(f[F_SYS_USEC], NULL, 10);
	jobsteps[js].rss = strtol(f[F_RSS], NULL,10);
	jobsteps[js].ixrss = strtol(f[F_IXRSS], NULL,10);
	jobsteps[js].idrss = strtol(f[F_IDRSS], NULL,10);
	jobsteps[js].isrss = strtol(f[F_ISRSS], NULL,10);
	jobsteps[js].minflt = strtol(f[F_MINFLT], NULL,10);
	jobsteps[js].majflt = strtol(f[F_MAJFLT], NULL,10);
	jobsteps[js].nswap = strtol(f[F_NSWAP], NULL,10);
	jobsteps[js].inblocks = strtol(f[F_INBLOCKS], NULL,10);
	jobsteps[js].oublocks = strtol(f[F_OUBLOCKS], NULL,10);
	jobsteps[js].msgsnd = strtol(f[F_MSGSND], NULL,10);
	jobsteps[js].msgrcv = strtol(f[F_MSGRCV], NULL,10);
	jobsteps[js].nsignals = strtol(f[F_NSIGNALS], NULL,10);
	jobsteps[js].nvcsw = strtol(f[F_NVCSW], NULL,10);
	jobsteps[js].nivcsw = strtol(f[F_NIVCSW], NULL,10);
	jobsteps[js].vsize = strtol(f[F_VSIZE], NULL,10);
	jobsteps[js].psize = strtol(f[F_PSIZE], NULL,10);

	if (jobs[j].job_terminated_seen == 0) {	/* If the job is still running,
						   this is the most recent
						   status */
		strcpy(jobs[j].finished, f[F_FINISHED]);
		jobs[j].cstatus[0] = 'r'; jobs[j].cstatus[1] = 0;
		if ( jobs[j].error == 0 )
			jobs[j].error = jobsteps[js].error;
		jobs[j].elapsed = time(NULL) - jobs[j].starttime;
	}
	/* now aggregate the aggregatable */
	jobs[j].tot_cpu_sec += jobsteps[js].tot_cpu_sec;
	jobs[j].tot_cpu_usec += jobsteps[js].tot_cpu_usec;
	jobs[j].tot_user_sec += jobsteps[js].tot_user_sec;
	jobs[j].tot_user_usec += jobsteps[js].tot_user_usec;
	jobs[j].tot_sys_sec += jobsteps[js].tot_sys_sec;
	jobs[j].tot_sys_usec += jobsteps[js].tot_sys_usec;
	jobs[j].inblocks += jobsteps[js].inblocks;
	jobs[j].oublocks += jobsteps[js].oublocks;
	jobs[j].msgsnd += jobsteps[js].msgsnd;
	jobs[j].msgrcv += jobsteps[js].msgrcv;
	jobs[j].nsignals += jobsteps[js].nsignals;
	jobs[j].nvcsw += jobsteps[js].nvcsw;
	jobs[j].nivcsw += jobsteps[js].nivcsw;
	/* and finally the maximums for any process */
	if (jobs[j].rss < jobsteps[js].rss)
		jobs[j].rss = jobsteps[js].rss;
	if (jobs[j].ixrss < jobsteps[js].ixrss)
		jobs[j].ixrss = jobsteps[js].ixrss;
	if (jobs[j].idrss < jobsteps[js].idrss)
		jobs[j].idrss = jobsteps[js].idrss;
	if (jobs[j].isrss < jobsteps[js].isrss)
		jobs[j].isrss = jobsteps[js].isrss;
	if (jobs[j].majflt < jobsteps[js].majflt)
		jobs[j].majflt = jobsteps[js].majflt;
	if (jobs[j].minflt < jobsteps[js].minflt)
		jobs[j].minflt = jobsteps[js].minflt;
	if (jobs[j].nswap < jobsteps[js].nswap)
		jobs[j].nswap = jobsteps[js].nswap;
	if (jobs[j].psize < jobsteps[js].psize)
		jobs[j].psize = jobsteps[js].psize;
	if (jobs[j].vsize < jobsteps[js].vsize)
		jobs[j].vsize = jobsteps[js].vsize;
}

void processJobTerminated(char *f[])
{
	long	i,
		j,
		job;

	job = strtol(f[F_JOB], NULL, 10);
	j = findJobRecord(job);
	if (j<0) {	/* fake it for now */
		j = initJobStruct(job,f);
		if (opt_verbose) 
			fprintf(stderr, "Note: JOB_TERMINATED record for job "
					"%ld preceded "
					"other job records at line %ld\n",
					job, lc);
	}
	if (jobs[j].job_terminated_seen) {
		fprintf(stderr, "Duplicate JOB_TERMINATED record for job "
				"%ld -- ignoring it\n",
				job);
		return;
	}
	jobs[j].job_terminated_seen = 1;
	jobs[j].elapsed = strtol(f[F_TOT_ELAPSED], NULL, 10);
	strcpy(jobs[j].finished, f[F_FINISHED]);
	strncpy(jobs[j].cstatus, f[F_CSTATUS], 3);
	for (i=0; jobs[j].cstatus[i]; i++)
		if (isspace(jobs[j].cstatus[i]))
				jobs[j].cstatus[i] = 0;
}


void usage(void)
{
	printf("\nUsage: sacct [options]\n\tUse --help for help\n");
}

void *_my_malloc(size_t size)
{
	void *tmp;
	tmp = (void *) malloc(size);
	if (tmp)
		return tmp;
	perror("malloc");
	exit(2);
}

void *_my_realloc(void *ptr, size_t size)
{
	void *tmp;
	tmp = (void *) realloc(ptr, size);
	if (tmp)
		return tmp;
	perror("realloc");
	exit(2);
}

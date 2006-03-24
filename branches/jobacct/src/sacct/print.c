/*****************************************************************************\
 *  print.c - print functions for sacct
 *
 *  $Id: print.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
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

#include "sacct.h"

char *_decode_status(char *cs);
char *_elapsed_time(long secs, long usecs);

char *_decode_status(char *cs)
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

char *_elapsed_time(long secs, long usecs)
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
		snprintf(daybuf, sizeof(daybuf), "%d-", days);
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

/* Field-specific print routines */

void print_cpu(type_t type, long idx)
{
	switch(type) {
	case HEADLINE:
		printf("%15s", "Cpu");
		break;
	case UNDERSCORE:
		printf("%15s", "---------------");
		break;
	case JOB:
		printf("%15s",
		       _elapsed_time(jobs[idx].tot_cpu_sec,
				   jobs[idx].tot_cpu_usec));
		break;
	case JOBSTEP:
		printf("%15s",
		       _elapsed_time(jobsteps[idx].tot_cpu_sec,
				   jobsteps[idx].tot_cpu_usec));
		break;
	} 
}

void print_elapsed(type_t type, long idx)
{
	switch(type) {
	case HEADLINE:
		printf("%15s", "Elapsed");
		break;
	case UNDERSCORE:
		printf("%15s", "---------------");
		break;
	case JOB:
		printf("%15s", _elapsed_time(jobs[idx].elapsed,0));
		break;
	case JOBSTEP:
		printf("%15s", _elapsed_time(jobsteps[idx].elapsed,0));
		break;
	} 
}

void print_error(type_t type, long idx)
{
	switch(type) {
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

void print_finished(type_t type, long idx)
{
	switch(type) {
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

void print_gid(type_t type, long idx)
{ 
	switch(type) {
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

void print_group(type_t type, long idx)
{ 
	switch(type) {
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

void print_idrss(type_t type, long idx)
{
	switch(type) {
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

void print_inblocks(type_t type, long idx)
{
	switch(type) {
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

void print_isrss(type_t type, long idx)
{
	switch(type) {
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

void print_ixrss(type_t type, long idx)
{
	switch(type) {
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

void print_job(type_t type, long idx)
{
	char	outbuf[12];
	switch(type) {
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

void print_name(type_t type, long idx)
{
	switch(type) {
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
		printf("%-18s", jobsteps[idx].stepname);
		break;
	} 
}

void print_step(type_t type, long idx)
{
	char	outbuf[12];
	switch(type) {
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

void print_majflt(type_t type, long idx)
{
	switch(type) {
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

void print_minflt(type_t type, long idx)
{
	switch(type) {
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

void print_msgrcv(type_t type, long idx)
{
	switch(type) {
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

void print_msgsnd(type_t type, long idx)
{
	switch(type) {
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

void print_ncpus(type_t type, long idx)
{ 
	switch(type) {
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

void print_nivcsw(type_t type, long idx)
{ 
	switch(type) {
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

void print_nodes(type_t type, long idx)
{ 
	switch(type) {
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

void print_nsignals(type_t type, long idx)
{ 
	switch(type) {
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

void print_nswap(type_t type, long idx)
{ 
	switch(type) {
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

void print_ntasks(type_t type, long idx)
{ 
	switch(type) {
	case HEADLINE:
		printf("%7s", "Ntasks");
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

void print_nvcsw(type_t type, long idx)
{ 
	switch(type) {
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

void print_outblocks(type_t type, long idx)
{ 
	switch(type) {
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

void print_partition(type_t type, long idx)
{ 
	switch(type) {
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

void print_psize(type_t type, long idx)
{ 
	switch(type) {
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

void print_rss(type_t type, long idx)
{ 
	switch(type) {
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

void print_status(type_t type, long idx)
{ 
	switch(type) {
	case HEADLINE:
		printf("%-10s", "Status");
		break;
	case UNDERSCORE:
		printf("%-10s", "----------");
		break;
	case JOB:
		printf("%-10s", _decode_status(jobs[idx].cstatus));
		break;
	case JOBSTEP:
		printf("%-10s", _decode_status(jobsteps[idx].cstatus));
		break;
	} 
}

void print_submitted(type_t type, long idx)
{ 
	switch(type) {
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

void print_systemcpu(type_t type, long idx)
{
	switch(type) {
	case HEADLINE:
		printf("%15s", "SystemCpu");
		break;
	case UNDERSCORE:
		printf("%15s", "---------------");
		break;
	case JOB:
		printf("%15s",
		       _elapsed_time(jobs[idx].tot_sys_sec,
				   jobs[idx].tot_sys_usec));
		break;
	case JOBSTEP:
		printf("%15s",
		       _elapsed_time(jobsteps[idx].tot_sys_sec,
				   jobsteps[idx].tot_sys_usec));
		break;
	} 

}

void print_uid(type_t type, long idx)
{ 
	switch(type) {
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

void print_user(type_t type, long idx)
{ 
	switch(type) {
	case HEADLINE:
		printf("%-9s", "User");
		break;
	case UNDERSCORE:
		printf("%-9s", "---------");
		break;
	case JOB:
	{
		char	*tmp="(unknown)";
		struct	passwd *pw;
		if ((pw=getpwuid(jobs[idx].uid)))
			tmp=pw->pw_name;
		printf("%-9s", tmp);
	}
	break;
	case JOBSTEP:
	{
		char	*tmp="(unknown)";
		struct	passwd *pw;
		if ((pw=getpwuid(jobs[jobsteps[idx].j].uid)))
			tmp=pw->pw_name;
		printf("%-9s", tmp);
	}
	break;
	} 
}

void print_usercpu(type_t type, long idx)
{
	switch(type) {
	case HEADLINE:
		printf("%15s", "UserCpu");
		break;
	case UNDERSCORE:
		printf("%15s", "---------------");
		break;
	case JOB:
		printf("%15s",
		       _elapsed_time(jobs[idx].tot_user_sec,
				   jobs[idx].tot_user_usec));
		break;
	case JOBSTEP:
		printf("%15s",
		       _elapsed_time(jobsteps[idx].tot_user_sec,
				   jobsteps[idx].tot_user_usec));
		break;
	} 

}

void print_vsize(type_t type, long idx)
{ 
	switch(type) {
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



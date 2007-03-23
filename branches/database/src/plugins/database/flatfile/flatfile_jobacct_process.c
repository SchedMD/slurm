/*****************************************************************************\
 *  flatfile_jobacct_process.h - functions the processing of
 *                               information from the flatfile jobacct
 *                               database.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "src/sacct/sacct.h"

void _destroy_exp(void *object)
{
	expired_rec_t *exp_rec = (expired_rec_t *)object;
	if(exp_rec) {
		xfree(exp_rec->line);
		xfree(exp_rec);
	}
}

/* prefix_filename() -- insert a filename prefix into a path
 *
 * IN:	path = fully-qualified path+file name
 *      prefix = the prefix to insert into the file name
 * RETURNS: pointer to the updated path+file name
 */

char *_prefix_filename(char *path, char *prefix) {
	char	*out;
	int     i,
		plen;

	plen = strlen(path);
	out = xmalloc(plen+strlen(prefix)+1);
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

/* _open_log_file() -- find the current or specified log file, and open it
 *
 * IN:		Nothing
 * RETURNS:	Nothing
 *
 * Side effects:
 * 	- Sets opt_filein to the current system accounting log unless
 * 	  the user specified another file.
 */

FILE *_open_log_file(char *logfile)
{
	FILE *fd = fopen(logfile, "r");
	if (fd == NULL) {
		perror(logfile);
		exit(1);
	}
	return fd;
}

char *_convert_type(int rec_type)
{
	switch(rec_type) {
	case JOB_START:
		return "JOB_START";
	case JOB_STEP:
		return "JOB_STEP";
	case JOB_TERMINATED:
		return "JOB_TERMINATED";
	default:
		return "UNKNOWN";
	}
}

int _cmp_jrec(const void *a1, const void *a2) {
	expired_rec_t *j1 = (expired_rec_t *) a1;
	expired_rec_t *j2 = (expired_rec_t *) a2;

	if (j1->job <  j2->job)
		return -1;
	else if (j1->job == j2->job) {
		if(j1->job_submit == j2->job_submit)
			return 0;
		else 
			return 1;
	}
	return 1;
}

void _show_rec(char *f[])
{
	int 	i;
	fprintf(stderr, "rec>");
	for (i=0; f[i]; i++)
		fprintf(stderr, " %s", f[i]);
	fprintf(stderr, "\n");
	return;
}

void _do_fdump(char* f[], int lc)
{
	int	i=0, j=0;
	char **type;
	char    *header[] = {"job",       /* F_JOB */
			     "partition", /* F_PARTITION */
			     "job_submit", /* F_JOB_SUBMIT */
			     "timestamp", /* F_TIMESTAMP */
			     "uid",	 /* F_UIDGID */
			     "gid",	 /* F_UIDGID */
			     "BlockID",  /* F_BLOCKID */
			     "reserved-2",/* F_RESERVED1 */
			     "recordType",/* F_RECTYPE */
			     NULL};

	char	*start[] = {"jobName",	 /* F_JOBNAME */ 
			    "TrackSteps", /* F_TRACK_STEPS */
			    "priority",	 /* F_PRIORITY */
			    "ncpus",	 /* F_NCPUS */
			    "nodeList", /* F_NODES */
				"account",   /* F_JOB_ACCOUNT */
			    NULL};
		
	char	*step[] = {"jobStep",	 /* F_JOBSTEP */
			   "status",	 /* F_STATUS */ 
			   "exitcode",	 /* F_EXITCODE */
			   "ntasks",	 /* F_NTASKS */
			   "ncpus",	 /* F_STEPNCPUS */
			   "elapsed",	 /* F_ELAPSED */
			   "cpu_sec",	 /* F_CPU_SEC */
			   "cpu_usec",	 /* F_CPU_USEC */
			   "user_sec",	 /* F_USER_SEC */
			   "user_usec",	 /* F_USER_USEC */
			   "sys_sec",	 /* F_SYS_SEC */
			   "sys_usec",	 /* F_SYS_USEC */
			   "rss",	 /* F_RSS */
			   "ixrss",	 /* F_IXRSS */
			   "idrss",	 /* F_IDRSS */
			   "isrss",	 /* F_ISRSS */
			   "minflt",	 /* F_MINFLT */
			   "majflt",	 /* F_MAJFLT */
			   "nswap",	 /* F_NSWAP */
			   "inblocks",	 /* F_INBLOCKS */
			   "oublocks",	 /* F_OUTBLOCKS */
			   "msgsnd",	 /* F_MSGSND */
			   "msgrcv",	 /* F_MSGRCV */
			   "nsignals",	 /* F_NSIGNALS */
			   "nvcsw",	 /* F_VCSW */
			   "nivcsw",	 /* F_NIVCSW */
			   "max_vsize",	 /* F_MAX_VSIZE */
			   "max_vsize_task",	 /* F_MAX_VSIZE_TASK */
			   "ave_vsize",	 /* F_AVE_VSIZE */
			   "max_rss",	 /* F_MAX_RSS */
			   "max_rss_task",	 /* F_MAX_RSS_TASK */
			   "ave_rss",	 /* F_AVE_RSS */
			   "max_pages",	 /* F_MAX_PAGES */
			   "max_pages_task",	 /* F_MAX_PAGES_TASK */
			   "ave_pages",	 /* F_AVE_PAGES */
			   "min_cputime",	 /* F_MIN_CPU */
			   "min_cputime_task",	 /* F_MIN_CPU_TASK */
			   "ave_cputime",	 /* F_AVE_RSS */
			   "StepName",	 /* F_STEPNAME */
			   "StepNodes",	 /* F_STEPNODES */
			   "max_vsize_node",	 /* F_MAX_VSIZE_NODE */
			   "max_rss_node",	 /* F_MAX_RSS_NODE */
			   "max_pages_node",	 /* F_MAX_PAGES_NODE */
			   "min_cputime_node",	 /* F_MIN_CPU_NODE */
			   "account",    /* F_STEP_ACCOUNT */
			   "requid",     /* F_STEP_REQUID */
			   NULL};
       
	char	*suspend[] = {"Suspend/Run time", /* F_TOT_ELAPSED */
			      "status",	 /* F_STATUS */ 
			      NULL};	 

	char	*term[] = {"totElapsed", /* F_TOT_ELAPSED */
			   "status",	 /* F_STATUS */ 
			   "requid",     /* F_JOB_REQUID */
			   NULL};	 
		
	i = atoi(f[F_RECTYPE]);
	printf("\n------- Line %d %s -------\n", lc, _convert_type(i));

	for(j=0; j < HEADER_LENGTH; j++) 
		printf("%12s: %s\n", header[j], f[j]);
	switch(i) {
	case JOB_START:
		type = start;
		j = JOB_START_LENGTH;
		break;
	case JOB_STEP:
		type = step;
		j = JOB_STEP_LENGTH;
		break;
	case JOB_SUSPEND:
		type = suspend;
		j = JOB_TERM_LENGTH;
	case JOB_TERMINATED:
		type = term;
		j = JOB_TERM_LENGTH;
		break;
	default:
		while(f[j]) {
			printf("      Field[%02d]: %s\n", j, f[j]); 
			j++;
		}
		return;
	}
	
	for(i=HEADER_LENGTH; i < j; i++)
       		printf("%12s: %s\n", type[i-HEADER_LENGTH], f[i]);	
}

extern List flatfile_jobacct_process_getdata(List selected_steps,
					     List selected_parts,
					     sacct_parameters_t *params)
{
	char line[BUFFER_SIZE];
	char *f[MAX_RECORD_FIELDS+1];    /* End list with null entry and,
					    possibly, more data than we
					    expected */
	char *fptr;
	int i;
	FILE *fd = NULL;
	int lc = 0;
	int rec_type = -1;
	selected_step_t *selected_step = NULL;
	char *selected_part = NULL;
	ListIterator itr = NULL;
	int show_full = 0;

	fd = _open_log_file(params->opt_filein);
	
	while (fgets(line, BUFFER_SIZE, fd)) {
		lc++;
		fptr = line;	/* break the record into NULL-
				   terminated strings */
				
		for (i = 0; i < MAX_RECORD_FIELDS; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL) {
				fptr = strstr(f[i], "\n");
				if (fptr)
					*fptr = 0;
				break; 
			} else
				*fptr++ = 0;
		}
		f[++i] = 0;
		
		if(i < HEADER_LENGTH) {
			continue;
		}
		
		rec_type = atoi(f[F_RECTYPE]);
		
		if (list_count(selected_steps)) {
			itr = list_iterator_create(selected_steps);
			while((selected_step = list_next(itr))) {
				if (strcmp(selected_step->job, f[F_JOB]))
					continue;
				/* job matches; does the step? */
				if(selected_step->step == NULL) {
					show_full = 1;
					list_iterator_destroy(itr);
					goto foundjob;
				} else if (rec_type != JOB_STEP 
					   || !strcmp(f[F_JOBSTEP], 
						      selected_step->step)) {
					list_iterator_destroy(itr);
					goto foundjob;
				} 
			}
			list_iterator_destroy(itr);
			continue;	/* no match */
		} else {
			show_full = 1;
		}
	foundjob:
		
		if (list_count(selected_parts)) {
			itr = list_iterator_create(selected_parts);
			while((selected_part = list_next(itr))) 
				if (!strcasecmp(f[F_PARTITION], 
						selected_part)) {
					list_iterator_destroy(itr);
					goto foundp;
				}
			list_iterator_destroy(itr);
			continue;	/* no match */
		}
	foundp:
		
		if (params->opt_fdump) {
			_do_fdump(f, lc);
			continue;
		}
		
		/* Build suitable tables with all the data */
		switch(rec_type) {
		case JOB_START:
			if(i < F_JOB_ACCOUNT) {
				printf("Bad data on a Job Start\n");
				_show_rec(f);
			} else 
				process_start(f, lc, show_full, i);
			break;
		case JOB_STEP:
			if(i < F_MAX_VSIZE) {
				printf("Bad data on a Step entry\n");
				_show_rec(f);
			} else
				process_step(f, lc, show_full, i);
			break;
		case JOB_SUSPEND:
			if(i < JOB_TERM_LENGTH) {
				printf("Bad data on a Suspend entry\n");
				_show_rec(f);
			} else
				process_suspend(f, lc, show_full, i);
			break;
		case JOB_TERMINATED:
			if(i < JOB_TERM_LENGTH) {
				printf("Bad data on a Job Term\n");
				_show_rec(f);
			} else
				process_terminated(f, lc, show_full, i);
			break;
		default:
			if (params->opt_verbose > 1)
				fprintf(stderr,
					"Invalid record at line %d of "
					"input file\n",
					lc);
			if (params->opt_verbose > 2)
				_show_rec(f);
			input_error++;
			break;
		}
	}
	
	if (ferror(fd)) {
		perror(params->opt_filein);
		exit(1);
	} 
	fclose(fd);

	return SLURM_SUCCESS;
	
	return NULL;
}

extern void flatfile_jobacct_process_do_expire(List selected_parts,
					       sacct_parameters_t *params)
{
	char	line[BUFFER_SIZE],
		*f[EXPIRE_READ_LENGTH],
		*fptr = NULL,
		*logfile_name = NULL,
		*old_logfile_name = NULL;
	int	file_err=0,
		new_file,
		i = 0;
	expired_rec_t *exp_rec = NULL;
	expired_rec_t *exp_rec2 = NULL;
	List keep_list = list_create(_destroy_exp);
	List exp_list = list_create(_destroy_exp);
	List other_list = list_create(_destroy_exp);
	struct	stat statbuf;
	mode_t	prot = 0600;
	uid_t	uid;
	gid_t	gid;
	FILE	*expired_logfile = NULL,
		*new_logfile = NULL;
	FILE *fd = NULL;
	int lc=0;
	int rec_type = -1;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *temp = NULL;

	/* Figure out our expiration date */
	time_t		expiry;
	expiry = time(NULL)-params->opt_expire;
	if (params->opt_verbose)
		fprintf(stderr, "Purging jobs completed prior to %d\n",
			(int)expiry);

	/* Open the current or specified logfile, or quit */
	fd = _open_log_file(params->opt_filein);
	if (stat(params->opt_filein, &statbuf)) {
		perror("stat'ing logfile");
		goto finished;
	}
	if ((statbuf.st_mode & S_IFLNK) == S_IFLNK) {
		fprintf(stderr, "%s is a symbolic link; --expire requires "
			"a hard-linked file name\n", params->opt_filein);
		goto finished;
	}
	if (!(statbuf.st_mode & S_IFREG)) {
		fprintf(stderr, "%s is not a regular file; --expire "
			"only works on accounting log files\n",
			params->opt_filein);
		goto finished;
	}
	prot = statbuf.st_mode & 0777;
	gid  = statbuf.st_gid;
	uid  = statbuf.st_uid;
	old_logfile_name = _prefix_filename(params->opt_filein, ".old.");
	if (stat(old_logfile_name, &statbuf)) {
		if (errno != ENOENT) {
			fprintf(stderr,"Error checking for %s: ",
				old_logfile_name);
			perror("");
			goto finished;
		}
	} else {
		fprintf(stderr, "Warning! %s exists -- please remove "
			"or rename it before proceeding\n",
			old_logfile_name);
		goto finished;
	}

	/* create our initial buffer */
	while (fgets(line, BUFFER_SIZE, fd)) {
		lc++;
		fptr = line;	/* break the record into NULL-
				   terminated strings */
		exp_rec = xmalloc(sizeof(expired_rec_t));
		exp_rec->line = xstrdup(line);
	
		for (i = 0; i < EXPIRE_READ_LENGTH; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL)
				break; 
			else
				*fptr++ = 0;
		}
		
		exp_rec->job = atoi(f[F_JOB]);
		exp_rec->job_submit = atoi(f[F_JOB_SUBMIT]);
		
		rec_type = atoi(f[F_RECTYPE]);
		/* Odd, but complain some other time */
		if (rec_type == JOB_TERMINATED) {
			if (expiry < atoi(f[F_TIMESTAMP])) {
				list_append(keep_list, exp_rec);
				continue;				
			}
			if (list_count(selected_parts)) {
				itr = list_iterator_create(selected_parts);
				while((temp = list_next(itr))) 
					if(!strcasecmp(f[F_PARTITION], temp)) 
						break;
				list_iterator_destroy(itr);
				if(!temp) {
					list_append(keep_list, exp_rec);
					continue;
				} /* no match */
			}
			list_append(exp_list, exp_rec);
			if (params->opt_verbose > 2)
				fprintf(stderr, "Selected: %8d %d\n",
					exp_rec->job,
					(int)exp_rec->job_submit);
		} else {
			list_append(other_list, exp_rec);
		}
	}
	if (!list_count(exp_list)) {
		printf("No job records were purged.\n");
		goto finished;
	}
	logfile_name = xmalloc(strlen(params->opt_filein)+sizeof(".expired"));
	sprintf(logfile_name, "%s.expired", params->opt_filein);
	new_file = stat(logfile_name, &statbuf);
	if ((expired_logfile = fopen(logfile_name, "a"))==NULL) {
		fprintf(stderr, "Error while opening %s", 
			logfile_name);
		perror("");
		xfree(logfile_name);
		goto finished;
	}

	if (new_file) {  /* By default, the expired file looks like the log */
		chmod(logfile_name, prot);
		chown(logfile_name, uid, gid);
	}
	xfree(logfile_name);

	logfile_name = _prefix_filename(params->opt_filein, ".new.");
	if ((new_logfile = fopen(logfile_name, "w"))==NULL) {
		fprintf(stderr, "Error while opening %s",
			logfile_name);
		perror("");
		fclose(expired_logfile);
		goto finished;
	}
	chmod(logfile_name, prot);     /* preserve file protection */
	chown(logfile_name, uid, gid); /* and ownership */
	/* Use line buffering to allow us to safely write
	 * to the log file at the same time as slurmctld. */ 
	if (setvbuf(new_logfile, NULL, _IOLBF, 0)) {
		perror("setvbuf()");
		fclose(expired_logfile);
		goto finished2;
	}

	list_sort(exp_list, (ListCmpF) _cmp_jrec);
	list_sort(keep_list, (ListCmpF) _cmp_jrec);
	
	if (params->opt_verbose > 2) {
		fprintf(stderr, "--- contents of exp_list ---");
		itr = list_iterator_create(exp_list);
		while((exp_rec = list_next(itr))) {
			if (!(i%5))
				fprintf(stderr, "\n");
			else
				fprintf(stderr, "\t");
			fprintf(stderr, "%d", exp_rec->job);
		}
		fprintf(stderr, "\n---- end of exp_list ---\n");
		list_iterator_destroy(itr);
	}
	/* write the expired file */
	itr = list_iterator_create(exp_list);
	while((exp_rec = list_next(itr))) {
		itr2 = list_iterator_create(other_list);
		while((exp_rec2 = list_next(itr2))) {
			if((exp_rec2->job != exp_rec->job) 
			   || (exp_rec2->job_submit != exp_rec->job_submit))
				continue;
			if (fputs(exp_rec2->line, expired_logfile)<0) {
				perror("writing expired_logfile");
				list_iterator_destroy(itr2);
				list_iterator_destroy(itr);
				fclose(expired_logfile);
				goto finished2;
			}
			list_remove(itr2);
			_destroy_exp(exp_rec2);
		}
		list_iterator_destroy(itr2);
		if (fputs(exp_rec->line, expired_logfile)<0) {
			perror("writing expired_logfile");
			list_iterator_destroy(itr);
			fclose(expired_logfile);
			goto finished2;
		}		
	}
	list_iterator_destroy(itr);
	fclose(expired_logfile);
	
	/* write the new log */
	itr = list_iterator_create(keep_list);
	while((exp_rec = list_next(itr))) {
		itr2 = list_iterator_create(other_list);
		while((exp_rec2 = list_next(itr2))) {
			if(exp_rec2->job != exp_rec->job)
				continue;
			if (fputs(exp_rec2->line, new_logfile)<0) {
				perror("writing keep_logfile");
				list_iterator_destroy(itr2);
				list_iterator_destroy(itr);
				goto finished2;
			}
			list_remove(itr2);
			_destroy_exp(exp_rec2);
		}
		list_iterator_destroy(itr2);
		if (fputs(exp_rec->line, new_logfile)<0) {
			perror("writing keep_logfile");
			list_iterator_destroy(itr);
			goto finished2;
		}		
	}
	list_iterator_destroy(itr);
	
	if (rename(params->opt_filein, old_logfile_name)) {
		perror("renaming logfile to .old.");
		goto finished2;
	}
	if (rename(logfile_name, params->opt_filein)) {
		perror("renaming new logfile");
		/* undo it? */
		if (!rename(old_logfile_name, params->opt_filein)) 
			fprintf(stderr, "Please correct the problem "
				"and try again");
		else
			fprintf(stderr, "SEVERE ERROR: Current accounting "
				"log may have been renamed %s;\n"
				"please rename it to \"%s\" if necessary, "
			        "and try again\n",
				old_logfile_name, params->opt_filein);
		goto finished2;
	}
	fflush(new_logfile);	/* Flush the buffers before forking */
	fflush(fd);
	
	file_err = slurm_reconfigure ();
	
	if (file_err) {
		file_err = 1;
		fprintf(stderr, "Error: Attempt to reconfigure "
			"SLURM failed.\n");
		if (rename(old_logfile_name, params->opt_filein)) {
			perror("renaming logfile from .old.");
			goto finished2;
		}

	}
	if (fseek(fd, 0, SEEK_CUR)) {	/* clear EOF */
		perror("looking for late-arriving records");
		goto finished2;
	}
	while (fgets(line, BUFFER_SIZE, fd)) {
		if (fputs(line, new_logfile)<0) {
			perror("writing final records");
			goto finished2;
		}
	}
	
	printf("%d jobs expired.\n", list_count(exp_list));
finished2:
	fclose(new_logfile);
	if (!file_err) {
		if (unlink(old_logfile_name) == -1)
			error("Unable to unlink old logfile %s: %m",
			      old_logfile_name);
	}
finished:
	fclose(fd);
	list_destroy(exp_list);
	list_destroy(keep_list);
	list_destroy(other_list);
	xfree(old_logfile_name);
	xfree(logfile_name);
}

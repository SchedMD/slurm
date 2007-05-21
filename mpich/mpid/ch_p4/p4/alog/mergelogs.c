/*
 * merge.c:  Program to take mutiple strand format logfiles, sort them,
 *           merge them, and prepend information to the final logfile.
 *
 * Algorithm:
 *     Copy all the negative events at the beginning of the logfiles to a
 *       temporary header file.
 *     Use the unix sort command to sort all the logfiles.
 *     Analyze the data, ignoring all events that are not > 0.
 *     Print to stdout the results of the analyzation, the header file,
 *       and the sorted log entries.
 *
 *	Modified Summer 1990 by James Arthur Kohl
 *		to combine count collection more efficiently.
 *
 *		Now it's REAL fast!
 */
#include <stdio.h>
#include <ctype.h>           /* for calling isdigit()             */
#include "alog_evntdfs.h"       /* Logfile definitions */
#if defined(SGI_CH) || defined(SGI_CH64)
#include <malloc.h>
#endif

#define C_DATA_LEN 50

#define DO_NEGATIVE 1
#define IGNORE_NEGATIVE 2

struct log_entry
{
    int proc_id;
    int task_id;
    int event;
    int i_data;
    char c_data[C_DATA_LEN];
    int time_slot;
    unsigned long time;
};

struct list_struct
{
    struct log_entry entry;
    struct list_struct *next;
} *log_table;

struct list_struct *log_ptr;

int entry_tot;

main(argc,argv)
int argc;
char *argv[];
{
    FILE *headerfp;

    char headerfile[255];

    int pid;

    if ( argc <= 1 ) 
	usage();

    pid = getpid();

    sprintf(headerfile,"/usr/tmp/log.header.%d",pid);

    if ( (headerfp=fopen(headerfile,"w")) == NULL )
    {
	fprintf(stderr,"merge: unable to create temp file %s.\n",headerfile);
	exit(0);
    }

    combine_files(argc,argv,headerfp);

    if ( (headerfp=fopen(headerfile,"r")) == NULL )
    {
	fprintf(stderr,"merge: unable to read temp file %s.\n",headerfile);
	exit(0);
    }

    fprintf(stderr,"Analyzing.\n");
    analyze(headerfp);

    fclose(headerfp);
    unlink(headerfile); 
} /* main */


combine_files(argc,argv,headerfp)
int argc;
char *argv[];
FILE *headerfp;
{
    FILE *in;

    struct list_struct **files;
    struct list_struct *last;
    struct list_struct *ptr;

    struct log_entry *entry;

    unsigned long min_time;

    int min_event;
    int min_slot;
    int min;

    int all_eof;
    int eof_flag;
    int negflag;
    int num;
    int i;

    num = argc - 1;

    if ( (files=(struct list_struct **)
	  malloc(num * sizeof(struct list_struct *))) == NULL )
    {
	fprintf(stderr,"merge: unable to allocate input data array.\n");
	exit(0);
    }

    for ( i=0 ; i < num ; i++ )
    {
	fprintf(stderr,"Reading %s\n",argv[i+1]);

	if ( (in=fopen(argv[i+1],"r")) == NULL )
	{
	    fprintf(stderr,"merge: unable to read data file %s.\n",argv[i+1]);
	    exit(0);
	}

	if ( (files[i]=(struct list_struct *)
	      malloc(sizeof(struct list_struct))) == NULL )
	{
	    fprintf(stderr,"merge: unable to allocate list struct.\n");
	    exit(0);
	}

	ptr = files[i];

	last = NULL;

	do
	{
	    read_logentry(in,&(ptr->entry),DO_NEGATIVE);

	    if ( !(eof_flag=feof(in)) )
	    {
		if ( (ptr->next=(struct list_struct *)
		      malloc(sizeof(struct list_struct))) == NULL )
		{
		    fprintf(stderr,"merge: unable to allocate list struct.\n");
		    exit(0);
		}

		last = ptr;

		ptr = ptr->next;
	    }

	    else
	    {
		if ( last != NULL )
		    last->next = NULL;

		else
		    files[i] = NULL;

		free(ptr);
		ptr = NULL;
	    }
	}
	while ( !eof_flag );

	fclose(in);
    }

    fprintf(stderr,"Sorting.\n");

    all_eof = 0;

    do
    {
	negflag = 0;

	for ( i=0 ; i < num ; i++ )
	{
	    if ( files[i] == NULL )
		continue;

	    entry = &(files[i]->entry);

	    if ( entry->event < 0 )
	    {
		negflag++;

		fprintf(headerfp,"%d %d %d %d %d %lu %s\n",entry->event,
			entry->proc_id,entry->task_id,entry->i_data,
			entry->time_slot,entry->time,entry->c_data);

		files[i] = files[i]->next;

		if ( files[i] == NULL )
		    all_eof++;
	    }
	}
    }
    while ( negflag && all_eof < num );

    fclose(headerfp);

    log_table = log_ptr = NULL;

    entry_tot = 0;

    while ( all_eof < num )
    {
	min_event = 0;
	min_time = 0;
	min_slot = 0;

	min = -1;

	for ( i=0 ; i < num ; i++ )
	{
	    if ( files[i] == NULL )
		continue;

	    entry = &(files[i]->entry);

	    if (
		(entry->time_slot == min_slot && entry->time < min_time)
		|| entry->time_slot < min_slot
		|| min == -1
		|| (entry->time_slot == min_slot && entry->time == min_time
		    && entry->event < min_event)
		)
	    {
		min_event = entry->event;
		min_time = entry->time;
		min_slot = entry->time_slot;

		min = i;
	    }
	}

	if ( log_ptr == NULL )
	{
	    log_table = log_ptr = files[min];

	    files[min] = files[min]->next;
	}

	else
	{
	    log_ptr->next = files[min];

	    files[min] = files[min]->next;

	    log_ptr = log_ptr->next;
	}

	log_ptr->next = NULL;

	entry_tot++;

	if ( files[min] == NULL )
	    all_eof++;
    }

    fprintf(stderr, "  %d total entries\n", entry_tot);
}


usage()
{
    fprintf(stderr,"mergelogs: mergelogs infile1 infile2 ...\n");
    fprintf(stderr,"  writes to stdout\n");

    exit(0);
}


/* analyze:  At this point, we have one large sorted file called :tname:.
 *           We want to prepend certain data to it.
 *           We also want to prepend the data from the file pointed to
 *             by :headerfp:.
 *           See balance:/usr/local/trace/README for info.
 *           
 */
analyze(headerfp)
FILE *headerfp;
{
    struct log_entry *entry;

    int proc_tot, task_tot, time_slot_tot, event_tot;
    int i;

    get_counts(&proc_tot,&task_tot,&event_tot,&time_slot_tot);

    fprintf(stderr, "  %d separate processors\n",  proc_tot);
    fprintf(stderr, "  %d separate tasks\n",  task_tot);
    fprintf(stderr, "  %d event types\n", event_tot);

    fprintf(stdout,"%d %d %d %d %d %lu\n",NUM_EVENTS,0,0,entry_tot,0,0L);
    fprintf(stdout,"%d %d %d %d %d %lu\n",NUM_PROCS,0,0,proc_tot,0,0L);
    fprintf(stdout,"%d %d %d %d %d %lu\n",NUM_TASKS,0,0,task_tot,0,0L);
    fprintf(stdout,"%d %d %d %d %d %lu\n",NUM_EVTYPES,0,0,event_tot,0,0L); 
    fprintf(stdout,"%d %d %d %d %d %lu\n",START_TIME,0,0,0,0,
	    log_table->entry.time);
    fprintf(stdout,"%d %d %d %d %d %lu\n",END_TIME,0,0,0,0,
	    log_ptr->entry.time);
    fprintf(stdout,"%d %d %d %d %d %lu\n",NUM_CYCLES,0,0,time_slot_tot,0,0L);

    dump_header(headerfp);

    log_ptr = log_table;

    while ( log_ptr != NULL )
    {
	entry = &(log_ptr->entry);

	fprintf(stdout,"%d %d %d %d %d %lu  %s\n",entry->event, 
		entry->proc_id,entry->task_id,entry->i_data,
		entry->time_slot,entry->time,entry->c_data);
	
	log_ptr = log_ptr->next;
    }
} /* analyze */  


dump_header(headerfp)
FILE *headerfp;
{
    char buf[512];
    int len;

    do
    {
	if ( len=fread(buf,sizeof(char),512,headerfp) )
	    fwrite(buf,sizeof(char),len,stdout);
    }
    while ( !feof(headerfp) && len );
} /* dump_header */


read_logentry(fp,table,do_negs)
FILE *fp;
struct log_entry *table;
int do_negs;
{
    char buf[81];
    char *cp;

    int i;

    do
    {	
	fscanf(fp,"%d %d %d %d %d %lu",
	       &(table->event),&(table->proc_id),&(table->task_id),
	       &(table->i_data),&(table->time_slot),&(table->time));

	cp = table->c_data;

	i = 0;

	do
	{
	    fscanf(fp,"%c",cp);
	}
	while ( *cp == ' ' || *cp == '\t' );

	i++;

	while ( *cp != '\n' && i < C_DATA_LEN )
	{
	    fscanf(fp,"%c",++cp);

	    i++;
	}

	*cp = '\0';

	/*
	   if ( !feof(fp) && table->event == 0 )
	   fprintf(stderr,"0 reading in.\n");
	   */
    }
    while( table->event < 0 && do_negs == IGNORE_NEGATIVE && !feof(fp) );
}


get_counts(ptot,ttot,etot,tstot)
int *ptot,*ttot,*etot,*tstot;
{
    struct list_struct *ptr;

    struct log_entry *entry;
    struct log_entry *last;

    int *p, *t, *e;

    int flag;
    int val;
    int i,j;

    if ( (p=(int *)malloc(sizeof(int) * entry_tot)) == NULL )
    {
	fprintf(stderr,"Not enough memory.\n");
	exit(0);
    }

    if ( (t=(int *)malloc(sizeof(int) * entry_tot)) == NULL )
    {
	fprintf(stderr,"Not enough memory.\n");
	exit(0);
    }

    if ( (e=(int *)malloc(sizeof(int) * entry_tot)) == NULL )
    {
	fprintf(stderr,"Not enough memory.\n");
	exit(0);
    }

    for ( i=0 ; i < entry_tot ; i++ )
    {
	p[i] = t[i] = e[i] = 0;
    }

    *ptot = *ttot = *etot = 0;

    *tstot = 1;

    ptr = log_table;

    last = NULL;

    while ( ptr != NULL )
    {
	entry = &(ptr->entry);

	flag = 0;

	val = entry->proc_id;

	for ( j=0 ; j < *ptot && !flag ; j++ )
	{
	    if ( p[j] == val )
		flag++;
	}

	if ( !flag )
	{
	    p[(*ptot)++] = val;
	}

	flag = 0;

	val = entry->task_id;

	for ( j=0 ; j < *ttot && !flag ; j++ )
	{
	    if ( t[j] == val )
		flag++;
	}

	if ( !flag )
	{
	    t[(*ttot)++] = val;
	}

	flag = 0;

	val = entry->event;

	for ( j=0 ; j < *etot && !flag ; j++ )
	{
	    if ( e[j] == val )
		flag++;
	}

	if ( !flag )
	{
	    e[(*etot)++] = val;
	}

	/* printf("tstot=%d *tstot=%d\n",tstot,*tstot); */
	if ( (last != NULL) && (entry->time_slot != last->time_slot) )
	    *tstot = *tstot + 1;
	/*  (*tstot)++;  */

	last = entry;

	ptr = ptr->next;
    }
    return(0);
}

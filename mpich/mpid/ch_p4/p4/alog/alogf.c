#include <strings.h>
#include <string.h>
#define ALOG_TRACE
#include "alog.h"
#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/* We've added prototypes for the Fortran interfaces just to suppress
   warning messages */

void alogfmaster_ ( int*, int * );
void alogfsetup_ ( int *, int * );
void alogfdefine_ ( int *, char *, char * );
void alogflog_ ( int *, int *, int *, char * );
void alogfoutput_ ( void );
void alogfstatus_ ( int * );
void alogfsetdir_ ( char * );
void alogfenable_ ( void );
void alogfdisable_ ( void );

void alogfmaster_(id,opt)		/* ALOG_MASTER */
int *id, *opt;
{
    /* printf("alogfm_: id=%d opt=%d\n",*id,*opt); */
    ALOG_MASTER(*id,*opt);
}

void alogfsetup_(id,opt)		/* ALOG_SETUP */
int *id, *opt;
{
    /* printf("alogfs_: id=%d opt=%d\n",*id,*opt); */
    ALOG_SETUP(*id,*opt);
}

void alogfdefine_(event,edef,fdef)	/* ALOG_DEFINE */
int *event;
char *edef, *fdef;
{
    /* printf("alogfd_: event=%d edef=%s fdef=%s\n",*event,edef,fdef); */
    ALOG_DEFINE(*event,edef,fdef);
}

void alogflog_(id,etype,data,string)	/* ALOG_LOG */
int *id, *etype, *data;
char *string;
{
    /* printf("alogl_: id=%d etype=%d data=%d string=%s\n",*id,*etype,*data,string); */
    ALOG_LOG(*id,*etype,*data,string);
}

void alogfoutput_()			/* ALOG_OUTPUT */
{
    /* printf("alogfo_: \n"); */
    ALOG_OUTPUT;
}

void alogfstatus_(status)			/* ALOG_STATUS */
int *status;
{
    /* printf("alogft_: \n"); */
    ALOG_STATUS(*status);
}

void alogfsetdir_(dir)			/* ALOG_SETDIR */
char *dir;
{
    /* printf("alogfr_: \n"); */
    ALOG_SETDIR(dir);
}

void alogfenable_()                         /* ALOG_ENABLE */
{
    /* printf("alogf1_: \n"); */
    ALOG_ENABLE;
}

void alogfdisable_()                        /* ALOG_DISABLE */
{
    /* printf("alogf1_: \n"); */
    ALOG_DISABLE;
}


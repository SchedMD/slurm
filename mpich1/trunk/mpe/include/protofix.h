/*
 * Gcc in particular tends to defer to the system's prototypes, even
 * when Gcc purports to provide them.  This file contains a set of
 * replacements for those needed in MPICH and MPE which may be used
 * for systems that don't provide correct or complete prototypes.
 */
#if defined(NEEDS_STDLIB_PROTOTYPES) || !defined(PROTOFIX_INCLUDED)
#define PROTOFIX_INCLUDED
#include <stdio.h>
/* 
   Some gcc installations have out-of-date include files and need these
   definitions to handle the "missing" prototypes.  This is NOT
   autodetected, but is provided and can be selected by using a switch
   on the options line.

   These are from stdlib.h, stdio.h, and unistd.h
 */
#include <sys/types.h>

/* stdio.h */
extern int fprintf(FILE*,const char*,...);
extern int printf(const char*,...);
extern int fflush(FILE *);
extern int fclose(FILE *);
extern int fscanf(FILE *,const char *,...);
extern int fputs(const char *,FILE *);
extern int sscanf(const char *, const char *, ... );
extern void perror(const char * );
extern int setvbuf (FILE *, char *, int, size_t);
/* extern int vfprintf (FILE *, const char *, ... ); *//* va_list */
extern int pclose (FILE *);

/* unistd.h */
extern unsigned int sleep ( unsigned int );
extern char *getenv (const char *);
extern int nice( int );
extern char *getwd (char *);

/* string.h */
extern char *strcat( char *, const char * );
extern char *strncpy(char *, const char *, size_t );
extern char *strncat(char *, const char *, size_t );
extern void *memset(void *, int, size_t);
extern void *memcpy(void *, const void *, size_t );

/* stdlib.h */
extern int system( const char * );
extern long int atol(const char *);
extern int atoi(const char *);
#ifndef malloc
/* In case malloc is being replaced ... */
extern void free(void *);
extern void *malloc( size_t );
#endif

/* time.h */
extern time_t   time (time_t *);
extern char     *ctime (const time_t *);
/* For test programs */
extern int main( int argc, char *argv[] );

#endif

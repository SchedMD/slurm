#define MAX_FILE_NAME 1024

#include "tools.h"
#include "system.h"
#include <stdio.h>
#include <string.h>
#ifndef __MSDOS__
#include <pwd.h>
#endif
#include <ctype.h>
/*
   Here's an unpleasent fact.  On Intel systems, include-ipsc/sys/types.h 
   contains "typedef long time_t" and
   include/time.h contains "typedef long int time_t".  We can fix this by 
   defining __TIME_T after types.h is included.
 */
#include <sys/types.h>
#if defined(intelnx) && !defined(intelparagon) && !defined(__TIME_T)
#define __TIME_T
#endif
#include <sys/stat.h>
/* Here's an unpleasent fact.  On Intel systems, unistd contains REDEFINITIONS
   of SEEK_SET, SEEK_CUR, and SEEK_END that are not guarded (in fact, the 
   unistd.h file contains no guards against multiple inclusion!).  */
#if defined(intelnx) && !defined(intelparagon)
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
#endif
#include <unistd.h>

extern char *mktemp();
extern char *getcwd();

/* WARNING - some systems don't have stdlib.h */
#if !defined(NOSTDHDR)
#include <stdlib.h>

#if defined(tc2000)
extern char *getenv();
#endif

#else
extern char *getenv();
#endif

#if defined(__MSDOS__)
typedef unsigned short u_short;
#endif
#if (defined(intelnx) && !defined(intelparagon)) || defined(__MSDOS__)
typedef u_short uid_t;
typedef u_short gid_t;
#endif

#ifndef __MSDOS__
/*@
   SYGetFullPath - Given a filename, return the fully qualified 
                 file name.

   Input parameters:
+   path     - pathname to qualify
.   fullpath - pointer to buffer to hold full pathname
-   flen     - size of fullpath

@*/
void SYGetFullPath( path, fullpath, flen )
char *path, *fullpath;
int  flen;
{
struct passwd *pwde;
int           ln;

if (path[0] == '/') {
    if (strncmp( "/tmp_mnt/", path, 9 ) == 0) 
	strncpy( fullpath, path + 8, flen );
    else 
	strncpy( fullpath, path, flen ); 
    return;
    }
SYGetwd( fullpath, flen );
strncat( fullpath,"/",flen - strlen(fullpath) );
if ( path[0] == '.' && path[1] == '/' ) 
strncat( fullpath, path+2, flen - strlen(fullpath) - 1 );
else 
strncat( fullpath, path, flen - strlen(fullpath) - 1 );

/* Remove the various "special" forms (~username/ and ~/) */
if (fullpath[0] == '~') {
    char tmppath[MAX_FILE_NAME];
    if (fullpath[1] == '/') {
	pwde = getpwuid( geteuid() );
	if (!pwde) return;
	strcpy( tmppath, pwde->pw_dir );
	ln = strlen( tmppath );
	if (tmppath[ln-1] != '/') strcat( tmppath+ln-1, "/" );
	strcat( tmppath, fullpath + 2 );
	strncpy( fullpath, tmppath, flen );
	}
    else {
	char *p, *name;

	/* Find username */
	name = fullpath + 1;
	p    = name;
	while (*p && isalnum(*p)) p++;
	*p = 0; p++;
	pwde = getpwnam( name );
	if (!pwde) return;
	
	strcpy( tmppath, pwde->pw_dir );
	ln = strlen( tmppath );
	if (tmppath[ln-1] != '/') strcat( tmppath+ln-1, "/" );
	strcat( tmppath, p );
	strncpy( fullpath, tmppath, flen );
	}
    }
/* Remove the automounter part of the path */
if (strncmp( fullpath, "/tmp_mnt/", 9 ) == 0) {
    char tmppath[MAX_FILE_NAME];
    strcpy( tmppath, fullpath + 8 );
    strcpy( fullpath, tmppath );
    }
/* We could try to handle things like the removal of .. etc */
}
#else
void SYGetFullPath( path, fullpath, flen )
char *path, *fullpath;
int  flen;
{
strcpy( fullpath, path );
}	
#endif



/*@
   SYGetRelativePath - Given a filename, return the relative path (remove
                 all directory specifiers)

   Input parameters:
+   fullpath  - full pathname
.   path      - pointer to buffer to hold relative pathname
-   flen     - size of path

@*/
void SYGetRelativePath( fullpath, path, flen )
char *path, *fullpath;
int  flen;
{
char  *p;

/* Find last '/' */
p = strrchr( fullpath, '/' );
if (!p) p = fullpath;
else    p++;
strncpy( path, p, flen );
}



#if !defined(__MSDOS__)
#include <sys/param.h>
#endif
#ifndef MAXPATHLEN
/* sys/param.h in intelnx does not include MAXPATHLEN! */
#define MAXPATHLEN 1024
#endif

/*@
   SYGetRealpath - get the path without symbolic links etc and in absolute
   form.

   Input Parameter:
.  path - path to resolve

   Output Parameter:
.  rpath - resolved path

   Note: rpath is assumed to be of length MAXPATHLEN

   Note: Systems that use the automounter often generate absolute paths
   of the form "/tmp_mnt....".  However, the automounter will fail to
   mount this path if it isn't already mounted, so we remove this from
   the head of the line.  This may cause problems if, for some reason,
   /tmp_mnt is valid and not the result of the automounter.
@*/
char *SYGetRealpath( path, rpath )
char *path, *rpath;
{
#if defined(sun4)
extern char *realpath();
realpath( path, rpath );

#elif defined(intelnx) || defined(__MSDOS__)
strcpy( rpath, path );

#else
#if defined(IRIX)
extern char *strchr();
#endif
  int  n, m, N;
  char tmp1[MAXPATHLEN], tmp3[MAXPATHLEN], tmp4[MAXPATHLEN], *tmp2;
  /* Algorithm: we move through the path, replacing links with the
     real paths.
   */
  strcpy( rpath, path );
#ifdef FOO  
  /* THIS IS BROKEN.  IT CAUSES INFINITE LOOPS ON IRIX, BECAUSE
     THE CODE ON FAILURE FROM READLINK WILL NEVER SET N TO ZERO */
  N = strlen(rpath);
  while (N) {
    strncpy(tmp1,rpath,N); tmp1[N] = 0;
    n = readlink(tmp1,tmp3,MAXPATHLEN);
    if (n > 0) {
      tmp3[n] = 0; /* readlink does not automatically add 0 to string end */
      if (tmp3[0] != '/') {
        tmp2 = strchr(tmp1,'/');
        m = strlen(tmp1) - strlen(tmp2);
        strncpy(tmp4,tmp1,m); tmp4[m] = 0;
        strncat(tmp4,"/",MAXPATHLEN - strlen(tmp4));
        strncat(tmp4,tmp3,MAXPATHLEN - strlen(tmp4));
        SYGetRealpath(tmp4,rpath);
        strncat(rpath,path+N,MAXPATHLEN - strlen(rpath));
        return rpath;
      }
      else {
        SYGetRealpath(tmp3,tmp1);
        strncpy(rpath,tmp1,MAXPATHLEN);
        strncat(rpath,path+N,MAXPATHLEN - strlen(rpath));
        return rpath;
      }
    }  
    tmp2 = strchr(tmp1,'/');
    if (tmp2) N = strlen(tmp1) - strlen(tmp2);
    else N = strlen(tmp1);
  }
  strncpy(rpath,path,MAXPATHLEN);
#endif
#endif
  if (strncmp( "/tmp_mnt/", rpath, 9 ) == 0) {
      char tmp3[MAXPATHLEN];
      strcpy( tmp3, rpath + 8 );
      strcpy( rpath, tmp3 );
      }
  return rpath;
}



#include <time.h>   /*I <time.h> I*/
/* Get the date that a file was last changed */
/*@
    SYLastChangeToFile - Gets the date that a file was changed as a string or
    timeval structure

    Input Parameter:
.   fname - name of file

    Output Parameters:
+   date - string to hold date (may be null)
-   ltm   - tm structure for time (may be null)
@*/
void SYLastChangeToFile( fname, date, ltm )
char      *fname, *date;
struct tm *ltm;
{
struct stat buf;
struct tm   *tim;

if (stat( fname, &buf ) == 0) {
    tim = localtime( &(buf.st_mtime) );
    if (ltm) *ltm = *tim;
    if (date) 
        sprintf( date, "%d/%d/%d", 
	         tim->tm_mon+1, tim->tm_mday, tim->tm_year+1900 );
    }
else {
    /* Could not stat file */	
    if (date)
        date[0] = '\0';
    if (ltm) {
    	ltm->tm_mon = ltm->tm_mday = ltm->tm_year = 0;
        }
    }
}



/*
    This file contains some simple routines for providing line-oriented input
    (and output) for FILEs.

    If you have a complicated input syntax and you can use lex and yacc,
    you should consider doing so.  Since lex and yacc produce C programs,
    their output is (roughly) as portable as this code (but DO NOT EXPECT
    yacc and lex to be the same on all systems!)
 */

/*@C
     SYTxtGetLine - Gets a line from a file.

     Input Parameters:
+    fp  - File
.    buffer - holds line read
-    maxlen - maximum length of text read

     Returns:
     The number of characters read.  -1 indicates EOF.  The \n is
     included in the buffer.
@*/
int SYTxtGetLine( fp, buffer, maxlen )
FILE *fp;
char *buffer;
int  maxlen;
{
int  nchar, c;

for (nchar=0; nchar<maxlen; nchar++) {
    buffer[nchar] = c = getc( fp );
    if (c == EOF || c == '\n') break;
    }
if (c == '\n') nchar++;    
buffer[nchar] = 0;    
if (nchar == 0 && c == EOF) nchar = -1;
/* fputs( buffer, stderr ); fflush( stderr ); */
return nchar;
}


/* Find the next space delimited token; put the text into token.
   The number of leading spaces is kept in nsp */
/*@C
    SYTxtFindNextToken - Finds the next space delimited token

    Input Parameters:
+   fp - FILE pointer
-   maxtoken - maximum length of token

    Output Parameters:
+   token - pointer to space for token
-   nsp   - number of preceeding blanks (my be null)

    Returns:
    First character in token or -1 for EOF.
@*/
int SYTxtFindNextToken( fp, token, maxtoken, nsp )
FILE *fp;
char *token;
int  maxtoken, *nsp;
{
int fc, c, Nsp, nchar;

Nsp = SYTxtSkipWhite( fp );

fc = c = getc( fp );
if (fc == EOF) return fc;
*token++ = c;
nchar    = 1;
if (c != '\n') {
    while (nchar < maxtoken && (c = getc( fp )) != EOF) {
	if (c != '\n' && !isspace(c)) {
	    *token++ = c;
	    nchar++;
	    }
	else break;
	}
    ungetc( (char)c, fp );
    }
*token++ = '\0';
*nsp     = Nsp;
return fc;
}


/*@C
    SYTxtSkipWhite - Skips white space but not newlines.

    Input Parameter:
.   fp - File pointer

    Returns:
    The number of spaces skiped    
@*/   
int SYTxtSkipWhite( fp )
FILE *fp;
{
int c;
int nsp;
nsp = 0;
while ((c = getc( fp )) != EOF) {
    if (!isspace(c) || c == '\n') break;
    nsp++;
    }
ungetc( (char)c, fp );
return nsp;
}

/*@C
    SYTxtDiscardToEndOfLine - Discards text until the end-of-line is read

    Input Parameter:
.   fp - File pointer    
@*/
void SYTxtDiscardToEndOfLine( fp )
FILE *fp;
{
int c;

while ((c = getc( fp )) != EOF && c != '\n') ;
}

/*@C
    SYTxtTrimLine - Copies a string  over itself, removing LEADING and 
                    TRAILING blanks.

    Input Parameters:
.   s - Pointer to string

    Returns:
    The final number of characters
@*/
int SYTxtTrimLine( s )
char *s;
{
char *s1 = s;
int  i;

while (*s1 && isspace(*s1)) s1++;
if (s1 != s) {
    for (i=0; s1[i]; i++) s[i] = s1[i];
    s[i] = '\0';
    }
i = strlen(s)-1;
while (i > 0 && isspace(s[i])) i--;
s[i+1] = '\0';
return i+1;
}


/*@C
    SYTxtUpperCase - Converts a string to upper case, in place.

    Input Parameter:
.   s - Pointer to string to convert
@*/
void SYTxtUpperCase( s )
char *s;
{
char c;
while (*s) {
    c = *s;
    if (isascii(c) && islower(c)) *s = toupper(c);
    s++;
    }
}


/*@C
  SYArgGetString - Get the value (string) of a named parameter.
  
  Input Parameters:
+ Argc  - pointer to argument count
. argv  - argument vector
. rflag - if true, remove the argument and its value from argv
. val   - pointer to buffer to hold value (will be set only if found).
- vallen- length of val
 
  Returns:
  1 on success
@*/
int SYArgGetString( Argc, argv, rflag, name, val, vallen )
int  *Argc, rflag, vallen;
char **argv, *name, *val;
{
int idx;

idx = SYArgFindName( *Argc, argv, name );
if (idx < 0) return 0;

if (idx + 1 >= *Argc) {
    SETERRC(1,"Missing value for argument" );
    return 0;
    }

strncpy( val, argv[idx+1], vallen );
if (rflag) {
    argv[idx]   = 0;
    argv[idx+1] = 0;
    SYArgSqueeze( Argc, argv );
    }
return 1;
}

/*@C
  SYArgHasName - Return 1 if name is in argument list
  
  Input Parameters:
+ Argc  - pointer to argument count
. argv  - argument vector
. rflag - if true, remove the argument and its value from argv
- name  - name to search for

  Returns:
  1 on success
@*/
int SYArgHasName( Argc, argv, rflag, name )
int  *Argc, rflag;
char **argv, *name;
{
int idx;

idx = SYArgFindName( *Argc, argv, name );
if (idx < 0) return 0;

if (rflag) {
    argv[idx]   = 0;
    SYArgSqueeze( Argc, argv );
    }
return 1;
}

/*@C
   SYArgSqueeze - Remove all null arguments from an arg vector; 
   update the number of arguments.
 @*/
void SYArgSqueeze( Argc, argv )
int  *Argc;
char **argv;
{
int argc, i, j;
    
/* Compress out the eliminated args */
argc = *Argc;
j    = 0;
i    = 0;
while (j < argc) {
    while (argv[j] == 0 && j < argc) j++;
    if (j < argc) argv[i++] = argv[j++];
    }
/* Back off the last value if it is null */
if (!argv[i-1]) i--;
*Argc = i;
}

/*@C
   SYArgFindName -  Find a name in an argument list.

   Input Parameters:
+  argc - number of arguments
.  argv - argument vector
-  name - name to find

   Returns:
   index in argv of name; -1 if name is not in argv
 @*/
int SYArgFindName( argc, argv, name )
int  argc;
char **argv;
char *name;
{
int  i;

for (i=0; i<argc; i++) {
    if (strcmp( argv[i], name ) == 0) return i;
    }
return -1;
}

/*@
  SYGetwd - Get the current working directory

  Input paramters:
+ path - use to hold the result value
- len  - maximum length of path
@*/
void SYGetwd( path, len )
char *path;
int  len;
{
#if defined(tc2000) || (defined(sun4) && !defined(solaris))
getwd( path );
#elif defined(__MSDOS__)
/* path[0] = 'A' + (_getdrive() - 1);
path[1] = ':';
_getcwd( path + 2, len - 2 );
 */
#if defined(__TURBOC__)
getcwd( path, len );
#else 
_getcwd( path, len );
#endif
#else
getcwd( path, len );
#endif
}


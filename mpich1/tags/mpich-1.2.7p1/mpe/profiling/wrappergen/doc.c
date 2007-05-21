
/*   Utility routines used by the document preparation system.     */

#include "tools.h"
#include "system.h"
#include <string.h>
#include "doc.h"

/* SubClass is the character after the KIND specifier */
static char SubClass    = ' ';
static int IsX11Routine = 0;

/*************************************************************************/

static int LineNo = 1;

/* This is a getc that keeps track of line numbers */
int DocGetChar( fd )
FILE *fd;
{
int c;
c = getc( fd );
if (c == '\n') LineNo++;
return c;
}

int ResetLineNo()
{
LineNo = 1;
}

int GetLineNo()
{
return LineNo;
}


/*************************************************************************/

char GetSubClass()
{
return SubClass;
}
int GetIsX11Routine()
{
return IsX11Routine;
}

/* Find a (non-alphanumeric) delimited token ----------------------*/
/* (After finding /*<char>, look for additional chars */
void FindToken( fd, token )
FILE *fd;
char *token;
{
int c;

IsX11Routine = 0;
c            = DocGetChar( fd );
SubClass     = c;
if (c == 'C') {
    c = DocGetChar( fd );
    }
if (c == 'X') IsX11Routine = 1;

while ((c = DocGetChar( fd )) != EOF)
    if (isalpha(c)) break;
*token++ = c;
while ((c = DocGetChar( fd )) != EOF) 
    if (!isspace(c)) *token++ = c;
    else break;
*token++ = '\0';
}

/* read chars until we find a leader (/*<character>) and a matching character.
   Then find the routine name (<name> - )
   Note that this routine does NOT use DocGetChar; this routine is
   the one that skips most of the text and needs to be faster.  */
int FoundLeader( fd, routine, kind )
FILE *fd;
char *routine, *kind;
{
int c;

while ((c = getc( fd )) != EOF) {
    if (c == '/') {
        if (MatchLeader( fd, MATCH_CHARS, kind )) {
	    FindToken( fd, routine );
	    return 1;
	    }    
        }    
    else if (c == '\n') LineNo++;
    }    
return 0;
}

/* Match a leader that starts with / * and then any of the selected
   characters.  Discards characters that don't match.  If we have
   entered this routine, we have already seen the first character (/) */
int MatchLeader( fd, Poss, kind )
FILE *fd;
char *Poss, *kind;
{ 
int c;
c = DocGetChar( fd );
if (c == '*') {
    /* In a comment.  We should really be prepared to skip this
       comment if we don't find that it is a documentation block */
    c = DocGetChar( fd );
    if (strchr( Poss, c )) {
        *kind = c;
        return 1;
        }
    }
return 0;
} 

/* 
  Copy an "include" to a buffer.  The form is / *I include-file-spec I* / 
  Only one per line.
 */
CopyIncludeName( fin, buffer )
FILE *fin;
char *buffer;
{
char *p;
int  c;

SkipWhite( fin );
p = buffer;

while ((c = DocGetChar(fin)) != EOF)  {
    if (c == '\n') break;
    if (c == 'I') {
	p[0] = 'I';
	c = DocGetChar(fin);
	if (c == '*') {
	    p[1] = '*';
	    c = DocGetChar(fin);
	    if (c == '/') { p--; break; }
	    p += 1;
	    }
	p += 1;
	}
    *p++ = c;
    }
while (p > buffer && *p == ' ') p--;
*++p = 0;
}

/* modifies the filename by converting it to the fullpath name and
   then removing the piece TOOLSDIR */
ExpandFileName(infilename,rlen)
char *infilename;
int  rlen;
{
  char tmp[MAX_FILE_SIZE],*name;
  name = tmp + rlen;
  SYGetFullPath(infilename,tmp,MAX_FILE_SIZE);
  if (rlen > 0) {
    strcpy(infilename,"$TOOLSDIR");
    strncat(infilename,name,MAX_FILE_SIZE-rlen-9);
  }
  else {
    strncpy(infilename,name,MAX_FILE_SIZE);
  }
    
return 0;
}

#ifndef MSDOS
#include <sys/param.h>
#endif
extern char *getenv();
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

/* returns the length of the full path name of the tools directory */
int GetToolsDirLength()
{
  char *toolsdir, truepath[MAXPATHLEN];
  int  rlen;

  toolsdir = getenv("TOOLSDIR");
  if (!toolsdir) return 0;
  else {
      SYGetRealpath(toolsdir,truepath);
      rlen = strlen(truepath);
      }
  return rlen;
}


/*
   This is a comparision routine that is independent of case
   str2 should be upper case; str1 need not be.
   Returns 0 if they match, != 0 otherwise
 */
int MatchTokens( str1, str2 )
char *str1, *str2;
{
char buf[256];

strcpy( buf, str1 );
UpperCase( buf );
return strcmp( buf, str2 );
}

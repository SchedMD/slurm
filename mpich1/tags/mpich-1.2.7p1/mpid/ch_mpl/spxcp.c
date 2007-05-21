/*
 * This code was provided by "Robert J. Harrison" <d3g681@fermi.emsl.pnl.gov>
 *
 * It has been SLIGHTLY modified by William Gropp.
 *
 * The purpose is to speed job startup on the SP by running jobs from 
 * the /tmp disks rather than from the same overburdened server.
 * This shouldn't be necessary, but it is.
 *
 * Usage:
 * node 0 copies the executable to local disk
 *  cp /sphome/harrison/nwchem /tmp/nwchem
 *
 * copy to all other nodes and mark as executable on all nodes
 * spxcp /tmp/nwchem
 *
 * fire up parallel task
 * /tmp/nwchem ...
 *
 * Compile and link with "mpcc -o copyexe copyexe.c".
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <mpproto.h>

static long dontcare, allmsg, nulltask, allgrp;

static void Error(const char *string, long code)
{
  (void) fflush(stdout);
  (void) fflush(stderr);

  (void) fprintf(stderr, "%3d:%s %ld(%x)\n", NODEID_(), string, code, code);
  (void) perror("system message");

  (void) fflush(stdout);
  (void) fflush(stderr);

  mpc_stopall(1L);
}

static void wildcards()
{
  long buf[4], qtype, nelem, status;
  qtype = 3;
  nelem = 4;
  status = mpc_task_query(buf,nelem,qtype);
  if(status==-1)
    Error("docopy: wildcards: mpc_task_query error", -1L);
  
  dontcare = buf[0];
  allmsg   = buf[1];
  nulltask = buf[2];
  allgrp   = buf[3];
}   

static long NODEID_()
/*\ Return number of the calling process ... at the moment this is
 *  just the same as the EUIH task numbering in allgrp
\*/
{
  long numtask, taskid, r;
  r= mpc_environ(&numtask, &taskid);
  return (taskid);
}

void BRDCST_(type, buf, lenbuf, originator)
     long *type;
     char *buf;
     long *lenbuf;
     long *originator;
/*
  broadcast buffer to all other processes from process originator
  ... all processes call this routine specifying the same
  orginating process.
*/
{
  long status;
  long me = NODEID_();
  long ttype = *type;

  status = mpc_bcast(buf, *lenbuf, *originator, allgrp);
  if(status == -1) 
      Error("BRDCST failed: mperrno error code ", mperrno);
}

static void docopy(const char *filename)
/*
  Process 0 has access to an executable named filename ... copy it
  to all other processes with the SAME name.
  (presumably fileout is in /tmp so the fixed path makes sense)
*/
{
  char *buffer;
  FILE *file;
  long length, nread=32768, len_nread=sizeof(long);
  long typenr  = 1;
  long typebuf = 2;

  if (!(buffer = malloc((unsigned) nread)))
    Error("docopy: failed to allocate the I/O buffer",nread);

  if (NODEID_() == 0) {

    /* I have the original file ... open and check its size */
    
    if ((file = fopen(filename,"r")) == (FILE *) NULL) {
      (void) fprintf(stderr,"me=%ld, filename = %s.\n",NODEID_(),filename);
      Error("docopy: 0 failed to open original file", 0L);
    }
    
    /* Quick sanity check on the length */

    (void) fseek(file, 0L, (int) 2);   /* Seek to end of file */
    length = ftell(file);              /* Find the length of file */
    (void) fseek(file, 0L, (int) 0);   /* Seek to beginning of file */
    if ( (length<0) || (length>1e9) )
      Error("docopy: the file length is -ve or very big", length);

    /* Send the file in chunks of nread bytes */

    while (nread) {
      nread = fread(buffer, 1, (int) nread, file);
      BRDCST_(&typenr, (char *) &nread, &len_nread, 0L);
      typenr++;
      if (nread) {
	BRDCST_(&typebuf, buffer, &nread, 0L);
	typebuf++;
      }
    }
  }
  else {
    
    /* Open the file for the duplicate */

    if ((file = fopen(filename,"w+")) == (FILE *) NULL) {
      (void) fprintf(stderr,"me=%ld, filename = %s.\n",NODEID_(),filename);
      Error("docopy: failed to open duplicate file", 0L);
    }
    
    /* Receive data and write to file */

    while (nread) {
      BRDCST_(&typenr, (char *) &nread, &len_nread, 0L);
      typenr++;
      if (nread) {
	BRDCST_(&typebuf, buffer, &nread, 0L);
	typebuf++;
	if (nread != fwrite(buffer, 1, (int) nread, file))
	  Error("docopy: error data to duplicate file", nread);
      }
    }
  }
  
  /* Tidy up the stuff we have been using */

  (void) fflush(file);
  (void) fclose(file);
  (void) free(buffer);
}

int main(int argc, char **argv)
{

  wildcards();   /* get the system wildcards */

  if (argc != 2) 
    Error("usage: spxcp filename\n", 0L);

  docopy(argv[1]);

  if (chmod(argv[1], (mode_t) 0755)) 
    Error("copyexe: chmod failed\n", 0L);

  mpc_sync(allgrp);

  return 0;
}

  
    
    



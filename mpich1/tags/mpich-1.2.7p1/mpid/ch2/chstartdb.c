
/* The following code has been added to start the debugger (ddd). To do this,
   the process forks a second (child) process. This child process execs the
   debugger, giving the debugger the PID of the parent (the MPI program).
   The debugger starts, attaches to and gains control of the parent.

   Lloyd Lewins, Hughes Aircraft Co.
*/

#define SPRINTF sprintf

{    int i, j;
     int startddd = 0;
     int pid;
     int rc;
     int sig;
     char masterpid[256];
     char *display;

/* Check for the command line argument "-ddd <display>" */
for (i = 1; i < *argc; ) {
   if (strcmp ((*argv)[i], "-ddd") == 0) {
      startddd = 1;
      for (j = i; j < *argc - 1; j++)
         (*argv)[j] = (*argv)[j+1];
      (*argc)--;
      if (i < *argc) {
         display = (*argv)[i];
         for (j = i; j < *argc - 1; j++)
            (*argv)[j] = (*argv)[j+1];
         (*argc)--;
      } else
         p4_error( "Display value missing after '-ddd'", 0 );
   } else
      i++;
      
   }

/* Start the debugger (ddd) if required */
if (startddd) { 

     /* Fork a new process for the debugger */
     pid = fork ();
 
     /* If pid is zero, we are the new process, otherwise we are the MPI
        program */
     if (pid == 0) {

	/* Create a new group, and make the debugger part of this group */
	setpgid (getpid(), getpid());

	/* Create a string containing the pid of the MPI program */
        
        SPRINTF (masterpid, "%u", getppid());

	/* Exec the debugger */
        rc = execlp (DDD, DDD, "-attach-source-window", "-debugger", 
                     "/home/9519/local/bin/gdb", 
                     "-display", display, (*argv)[0], 
                     masterpid, NULL);

	/* We should never get to here!! */
        if (rc < 0)
           perror("execlp");
	exit (1);

     } else {
	/* Wait for the debugger to resume us. We require the user to
           enter the command "signal SIGINT" instead of the usual
           command "continue". 
        */
        signal (SIGINT, intrhandler);
        sig = pause ();
        signal (SIGINT, SIG_DFL);
     }
   }
}

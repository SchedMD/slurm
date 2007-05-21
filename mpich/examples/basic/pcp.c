/* pcp from SUT, in MPI */
#include "mpi.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define BUFSIZE    256*1024
#define CMDSIZE    80

char from_path[1024], fromname[1024],
     to_path[1024], toname[1024],
     origdir[1024], cmd[1024];

int main( int argc, char *argv[] )
{
    int myrank, mystatus, allstatus, done, numread;
    char controlmsg[CMDSIZE];
    int statrc, infd, outfd;
    char *c, buf[BUFSIZE];
    struct stat statbuf;
    FILE *infp, *outfp;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &myrank );

    if (getcwd(origdir,1024) == NULL)
    {
        fprintf(stderr,"failed to getcwd\n");
	exit(-1);
    }

    if ( myrank == 0 ) {
        if ((c = (char*)strrchr(argv[1],'/')) != NULL)
        {
            strncpy(from_path,argv[1],c-argv[1]);
            from_path[c-argv[1]] = '\0'; 
            strcpy(fromname,c+1);
            fromname[strlen(c)] = '\0'; 
            chdir(from_path);
        }
	else
	{
	    strcpy(fromname,argv[1]);
	}
	sprintf(cmd,"tar cf - %s",fromname);
        if ((infp = popen(cmd,"r")) == NULL)
        {
            fprintf(stderr,"popen r failed\n");
	    strcpy( controlmsg, "exit" );
	    MPI_Bcast( controlmsg, CMDSIZE, MPI_CHAR, 0, MPI_COMM_WORLD );
	    MPI_Finalize();
            exit(-1);
        }
	else {
            infd = fileno(infp);
	    sprintf( controlmsg, "ready" );
	    MPI_Bcast( controlmsg, CMDSIZE, MPI_CHAR, 0, MPI_COMM_WORLD );
        }
    }
    else {
        MPI_Bcast( controlmsg, CMDSIZE, MPI_CHAR, 0, MPI_COMM_WORLD );
        if ( strcmp( controlmsg, "exit" ) == 0 ) {
	    MPI_Finalize();
	    exit( -1 );
	}
    }

    chdir(origdir);

    if ( myrank == 0 ) 
        strcpy( controlmsg, fromname );
    MPI_Bcast( controlmsg, CMDSIZE, MPI_CHAR, 0, MPI_COMM_WORLD );
    strcpy(fromname,controlmsg);
    strcpy(toname,argv[2]);

    statrc = stat(argv[2], &statbuf);
    if (statrc >= 0)
    {
        if (S_ISDIR(statbuf.st_mode))
        {
            chdir(argv[2]);
        }
    }
    if ((c = (char*)strrchr(argv[2],'/')) != NULL)
    {
	strncpy(to_path,argv[2],c-argv[2]);
	to_path[c-argv[2]] = '\0';
	strcpy(toname,c+1);
	toname[strlen(c)] = '\0';
	chdir(to_path);
    }

    sprintf(cmd,"tar xf - ");
    if ((outfp = popen(cmd,"w")) == NULL)
    {
	fprintf(stderr,"popen w failed\n");
        mystatus = -1;
    }
    else {
	outfd = fileno(outfp);
        mystatus = 0;
    }
    MPI_Allreduce( &mystatus, &allstatus, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD );
    if ( allstatus == -1 ) {
        if ( myrank == 0 )
	    fprintf(stderr,"output file %s could not be opened\n",
		    controlmsg  );
	MPI_Finalize();
	return( -1 );
    }

    /* at this point all files have been successfully opened */
    
    done = 0;
    while ( !done ) {
        if ( myrank == 0 )
	    numread = read( infd, buf, BUFSIZE );
        MPI_Bcast( &numread, 1, MPI_INT, 0, MPI_COMM_WORLD );

	if ( numread > 0 ) {
	    MPI_Bcast( buf, numread, MPI_BYTE, 0, MPI_COMM_WORLD );
	    write( outfd, buf, numread ); /* master makes a copy too */
	}
	else {	  
            if ( myrank == 0 )
                pclose(infp);
	    pclose( outfp );
	    done = 1;
	}
    }
    /* if file existed but was not a dir */
    if (statrc < 0  ||  ! S_ISDIR(statbuf.st_mode))
    {
        if (strcmp(fromname,toname) != 0)
	{
	    rename(fromname,toname);
	}
    }
    MPI_Finalize();
    return 0;
}

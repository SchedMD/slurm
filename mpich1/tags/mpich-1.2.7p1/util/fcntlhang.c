#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define FILENAME "test"
int pid;

void handler( sig )
{
    pid = 0;
}

main(int argc, char **argv)
{
    int fd, rank, buf[10000], fsize;
    int err;
    struct flock lock;

    /* Create a process that can timeout */
    signal( SIGCLD, handler );
    pid = fork();
    if (pid) { 
	sleep( 15 );
	if (pid) {
	    printf( "Child process hung\n" );
	    kill( pid, SIGINT );
	    sleep( 3 );
	    kill( pid, SIGKILL );
	}
    }
    else {
	/* Create another process */
	signal( SIGCLD, SIG_IGN );
	pid = fork();
        
	rank = (pid == 0);
	printf( "Created process with rank %d\n", rank );

	fd = open(FILENAME, O_CREAT | O_RDWR, 0644);

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 10000*sizeof(int);

	err = fcntl(fd, F_SETLKW, &lock);
	if (err && errno == EINTR) {
	    printf( "Hung in fcntl\n" );
	    if (pid) kill(pid,SIGINT);
	    return 1;
	}
	if (!rank) write(fd, buf, 10000*sizeof(int));
	else read(fd, buf, 10000*sizeof(int));
	
	lock.l_type = F_UNLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 10000*sizeof(int);
    
	err = fcntl(fd, F_SETLK, &lock);
	if (err && errno == EINTR) {
	    printf( "Hung in fcntl\n" );
	    if (pid) kill(pid,SIGINT);
	    return 1;
	}
	
	close(fd);
    }

    return 0;
}


/* Test pmdv3 program */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

main (int argc, char **argv)
{
	int log_fd, rc = 0;
	ssize_t size;
	char buf[1024];

	sprintf(buf, "/tmp/mplog.%d", getpid());
	log_fd = creat(buf, 0755);
	if (log_fd < 0) {
		perror("creat");
		exit(1);
	}	

	while (1) {
		size = read(0, buf, sizeof(buf));
		if (size < 0) {
			sprintf(buf, "read errno=%d\n", errno);
			write(log_fd, buf, strlen(buf));
			if (errno == EAGAIN)
				continue;
			rc = 1;
			break;
		}
		if (size == 0) {
			sprintf(buf, "read EOF\n");
			write(log_fd, buf, strlen(buf));
			break;
		}
		write(log_fd, buf, size);
	}

	exit(rc);
}


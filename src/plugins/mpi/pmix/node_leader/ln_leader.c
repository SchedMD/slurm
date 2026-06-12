#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
extern int errno;
#include <string.h>
#include <dirent.h>

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>
#include <stdlib.h>
#include <poll.h>

#include "fileops.h"

/* Slurm portability */
#define SLURM_SUCCESS 0
#define SLURM_ERROR 1
/* TODO: fix */
#define FILENAME_PREFIX "pmix_addr"

typedef struct {
    int jobid, stepid;
} local_records_t;

int my_jobid = -1;
int my_stepid = -1;
int sfd = -1, lockfd = -1;

char linkname[FILENAME_MAX];
char usockname[FILENAME_MAX], lockname[FILENAME_MAX];

int prepare_srv_socket(char *path);
void establish_listen_sock(int jobid, int stepid);

int pid_from_usockname(char *us_name, int jobid);
int run_discovery(int jobid, int stepid, int *is_leader);

int main(int argc, char **argv)
{
    int is_leader;
    if (argc < 3) {
        fprintf(stderr,"Not enough arguments\n");
        exit(0);
    }
    my_jobid = atoi(argv[1]);
    my_stepid = atoi(argv[2]);
    snprintf(linkname, FILENAME_MAX, "%s.%d", FILENAME_PREFIX, my_jobid);
    snprintf(usockname, FILENAME_MAX, "%s.%d", linkname, my_stepid);
    snprintf(lockname, FILENAME_MAX, "%s.lock",usockname);

    if (0 > (lockfd = pmix_create_locked(lockname))) {
        fprintf(stderr,"Can't create lock file %s\n", lockname);
        exit(0);
    }

    sfd = prepare_srv_socket(usockname);

    int i = 0;
    while(1) {
        int fd = run_discovery(my_jobid, my_stepid, &is_leader);
        if (is_leader) {
            fprintf("Iteration %d. I am the leader\n", i);
            service_requests(fd);
        } else {
            monitor_leader(fd);
            printf("server closed connection. Repeat resolution\n");
        }
        i++;
    }
}

int run_discovery(int jobid, int my_stepid, int *is_leader)
{
    char lname[FILENAME_MAX], fname[FILENAME_MAX], fname1[FILENAME_MAX];

    *is_leader = 0;
    if (!pmix_leader_is_alive(linkname)) {
        pmix_remove_leader_symlink(linkname);
        if (!symlink(usockname, linkname)) {
            *is_leader = 1;
            return sfd;
        }
    }
    return connect_to_server(linkname);
}


int prepare_srv_socket(char *path)
{
    static struct sockaddr_un sa;
    int ret = 0;

    if (strlen(path) >= sizeof(sa.sun_path)) {
        /*PMIXP_ERROR_STD*/
        printf("UNIX socket path is too long: %lu, max %lu",
               (unsigned long)strlen(path),
               (unsigned long)sizeof(sa.sun_path)-1);
        return SLURM_ERROR;
    }

    /* Make sure that socket file doesn't exists */
    if (0 == access(path, F_OK)) {
        /* remove old file */
        if (0 != unlink(path)) {
            /*PMIXP_ERROR_STD*/
            printf("Cannot delete outdated socket fine: %s",
                    path);
            return SLURM_ERROR;
        }
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        /*PMIXP_ERROR_STD*/
        printf("Cannot create UNIX socket");
        return SLURM_ERROR;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);
    if (ret = bind(fd, (struct sockaddr*)&sa, SUN_LEN(&sa))) {
        /*PMIXP_ERROR_STD*/
        printf("Cannot bind() UNIX socket %s", path);
        goto err_fd;
    }

    if ((ret = listen(fd, 64))) {
        /*PMIXP_ERROR_STD*/
        printf("Cannot listen(%d, 64) UNIX socket %s", fd, path);
        goto err_bind;
    }
    return fd;

err_bind:
    unlink(path);
err_fd:
    close(fd);
    return ret;
}

int connect_to_server(char *path)
{
    static struct sockaddr_un sa;

    if (strlen(path) >= sizeof(sa.sun_path)) {
        /*PMIXP_ERROR_STD*/
        printf("UNIX socket path is too long: %lu, max %lu",
               (unsigned long)strlen(path),
               (unsigned long)sizeof(sa.sun_path)-1);
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr,"Cannot create UNIX socket");
        return -1;
    }

    if (connect(fd, &sa, SUN_LEN(&sa))) {
        close(fd);
        return -1;
    }
    local_records_t rec;
    rec.jobid = my_jobid;
    rec.stepid = my_stepid;
    int ret = write(fd, &rec, sizeof(rec));
    if (sizeof(rec) != ret) {
        close(fd);
        return -1;
    }
    return fd;
}

void service_requests(int fd)
{
    while(1) {
        int cfd;
        if (0 < (cfd = accept(fd, NULL, 0))) {
            local_records_t rec;
            int ret = read(cfd, &rec, sizeof(rec));
            if (ret != sizeof(rec)) {
                fprintf("%s:%d: read mismatch: %d vs %d\n",
                        __FILE__, __LINE__, ret, sizeof(rec));
                exit(0);
            }
            fprintf(stderr,"New client connected: jobid=%d, stepid=%d, fd = %d\n",
                   rec.jobid, rec.stepid, fd);
        }
    }
}

void monitor_leader(int fd)
{
    struct pollfd fds;
    fds.fd = fd;
    fds.events = 0;

    /* Drop shutdown before the check */

    int rc = poll(&fds, 1, -1);
    if (rc < 0) {
        fprintf(stderr,"Get poll error %d: %s", errno, strerror(errno));
        exit(1);
    }
    if (fds.revents != POLLHUP) {
        fprintf(stderr,"revents = %x", fds.revents);
    }
}

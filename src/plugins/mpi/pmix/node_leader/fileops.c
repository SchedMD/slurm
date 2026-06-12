#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
extern int errno;
#include <errno.h>
#include <stdlib.h>


/* Check the file and return:
 * - 1 in case it exists
 * - 0 if not exists
 * - (-1) in case of unexpected error, like EPERM)
 * if file exists is_symlink variable is set if the file
 * is a symlink
 */
static int
_file_exists(char *path)
{
    struct stat buf;

    if (stat(path, &buf)) {
        if (errno == ENOENT) {
            /* OK. this symlink was removed by somebody else */
            return 0;
        }else{
            fprintf(stderr,"Error while stat'ing symlink %s: %s (%d)\n",
                    path, strerror(errno), errno);
            return -1;
        }
    }
    return 1;
}

static int
_symlink_exists(char *path)
{
    struct stat buf;

    if (lstat(path, &buf)) {
        return 0;
    }

    if (!S_ISLNK(buf.st_mode)) {
        return -1;
    }
    return 1;
}

static int
_create_locked_cmd(char *path, int cmd)
{
    int ret = 0, fd;
    struct flock flk;

    fd = open(path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (0 > fd) {
        fprintf(stderr,"Error opening file %s: %s (%d)\n",
                path, strerror(errno), errno);
        return -1;
    }


    flk.l_whence = SEEK_SET;
    flk.l_start = 0;
    flk.l_len = 0;
    flk.l_type = F_WRLCK;

    do{
        if (0 == (ret = fcntl(fd, cmd, &flk))) {
            break;
        } else if (EINTR != errno) {
            fprintf(stderr,"Error locking file %s: %s (%d)\n",
                    path, strerror(errno), errno);
            close(fd);
            return -errno;
        }
    } while(EINTR == errno);

    if (0 == ret)
        return fd;

    close(fd);
    return ret;
}

static int
_is_locked(char *path)
{
    int ret, fd;
    struct flock flk;
    memset(&flk, 0, sizeof(flk));

    fd = open(path, O_RDONLY);
    if (0 > fd) {
        if (EEXIST == errno) {
            return 0;
        } else {
            fprintf(stderr,"Error opening file %s: %s (%d)\n",
                    path, strerror(errno), errno);
            return -1;
        }
    }

    do{
        if (0 == (ret = fcntl(fd, F_GETLK, &flk))) {
            break;
        }
    } while(EINTR == errno);

    if (ret) {
        fprintf(stderr,"Error getting file lock information for %s: %s (%d)\n",
                path, strerror(errno), errno);
        return -1;
    }

    if (F_WRLCK == flk.l_type && SEEK_SET == flk.l_whence) {
        return 1;
    }
    return 0;
}

int pmix_leader_is_alive(char *lname)
{
    int ret, error;
    char fname[FILENAME_MAX] = { 0 };
    char lock_name[FILENAME_MAX] = { 0 };

    ret = _symlink_exists(lname);
    if (0 > ret) {
        fprintf(stderr,"Error accessing symlink %s: %d:%s\n",
                lname, error, strerror(error));
        return -1;
    }
    if (0 > readlink(lname, fname, FILENAME_MAX - 1)) {
        if (ENOENT == errno) {
            /* Symlink was removed between _symlink_exists & readlink */
            return 0;
        } else {
            fprintf(stderr,"Error reading symlink %s: %d:%s\n",
                    lname, error, strerror(error));
            return -1;
        }
    }

    snprintf(lock_name, FILENAME_MAX - 1, "%s.lock",fname);
    ret = _file_exists(lock_name);
    /* In case of fatal error (ret < 0) or file abscense */
    if (0 >= ret)
        return ret;
    return _is_locked(lock_name);
}

void pmix_remove_leader_symlink(char *path)
{
    int ret, is_symlink;

    /* Check prior to go further */
    ret = _symlink_exists(path);
    if (0 > ret) {
        fprintf(stderr,"FATAL error\n");
        exit(1);
    }
    if (0 == ret) {
        /* File was already deleted. Nothing to do */
        return;
    }

    char lockname[FILENAME_MAX];
    snprintf(lockname, FILENAME_MAX, "%s.lock", path);
    int fd = pmix_create_locked_wait(lockname);

    /* Check prior to go further */
    ret = _symlink_exists(path);
    if (0 > ret) {
        fprintf(stderr,"FATAL error\n");
        exit(1);
    }
    if (0 == ret) {
        /* File was already deleted. Nothing to do */
        goto exit;
    }

    if (pmix_leader_is_alive(path)) {
        /* Somebody already took care about this */
        goto exit;
    }

    if (unlink(path)) {
        if (ENOENT != errno) {
            fprintf(stderr, "unlink(%s): %s", path, strerror(errno));
            exit(1);
        }
    }

exit:
    close(fd);
}

int pmix_create_locked(char *path)
{
    return _create_locked_cmd(path, F_SETLK);
}

int pmix_create_locked_wait(char *path)
{
    return _create_locked_cmd(path, F_SETLKW);
}


#if 0

#define FPREFIX "test"

int main(int argc, char **argv)
{
    char lockfile[FILENAME_MAX], basefile[FILENAME_MAX];
    if (2 > argc) {
        fprintf(stderr, "Not enough parameters\n");
        exit(0);
    }

    snprintf(basefile, FILENAME_MAX, FPREFIX ".%s", argv[1]);
    snprintf(lockfile, FILENAME_MAX, "%s.lock", basefile);
    if (0 > _create_locked(lockfile)) {
        fprintf(stderr, "Cannot create personal lock file %s\n", basefile);
        exit(0);
    }

    int fd = open(basefile,O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        fprintf(stderr,"Cannot create basefile\n");
        exit(0);
    }

    while(1) {
        if (!pmix_leader_is_alive(FPREFIX)) {
            pmix_remove_leader_symlink(FPREFIX);
            if (!symlink(basefile, FPREFIX)) {
                while(1) {
                    sleep(1);
                }
            }
        }
        sleep(1);
    }
}
#endif

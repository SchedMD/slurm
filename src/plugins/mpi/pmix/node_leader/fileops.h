#ifndef FILEOPS_H
#define FILEOPS_H

int pmix_leader_is_alive(char *lname);
void pmix_remove_leader_symlink(char *path);
int pmix_create_locked(char *path);
int pmix_create_locked_wait(char *path);

#endif /* FILEOPS_H */

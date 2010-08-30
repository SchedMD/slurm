/* Includes to allow use of datetoi.c without modification. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define LRM_EINVAL EINVAL
#define MAX(a, b) (a > b ? a : b)
#define streq(x, y)       (strcmp((x), (y)) == 0)
#define nstreq(x, y, n)   (strncmp((x), (y), (n)) == 0)

##*****************************************************************************
#  AUTHOR:
#    Tim Wickberg <tim@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_UID_GID_SIZE_CHECK
#
#  DESCRIPTION:
#    Slurm assumes sizeof(uid_t) == sizeof(gid_t) == sizeof(uint32_t).
#    Enforce that at compile time to prevent potential security problems.
#
#    sizeof(uid) > sizeof(uint64_t) leads to potential truncation problems.
#
#    sizeof(gid) < sizeof(uint64_t) is less obvious, but a worse security risk.
#    Internally, arrays of gid_t are serialized as uint32_t.
#    If sizeof(gid_t) == sizeof(uint16_t), Slurm would call setgroups() with
#    with every other element of the array being interpretted as zero, a.k.a.
#    the group id for root.
##*****************************************************************************

AC_DEFUN([X_AC_UID_GID_SIZE_CHECK], [
  AC_MSG_CHECKING([for uid_t and gid_t data sizes)])
  AC_RUN_IFELSE([AC_LANG_SOURCE([
	#include <inttypes.h>
	#include <sys/types.h>
	int main(int argc, char **argv)
	{
		if (sizeof(uid_t) != sizeof(uint32_t) ||
		    sizeof(gid_t) != sizeof(uint32_t))
			return -1;
		return 0;
	}
    ])],
    uid_t_size_ok=yes,
    uid_t_size_ok=no,
    uid_t_size_ok=yes)

  if test "$uid_t_size_ok" = "no"; then
    AC_MSG_ERROR([unexpected size for uid_t or gid_t])
  else
    AC_MSG_RESULT([yes])
  fi
])

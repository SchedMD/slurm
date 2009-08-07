#
# Makefile for SLURM PAM library
#

AUTOMAKE_OPTIONS = foreign

CFLAGS = -fPIC
INCLUDES = -I$(top_srcdir)/src -I$(top_srcdir)/..
LIBS =
# Without "-module", we get error: `pam_slurm.la' is not a standard libtool library name
PLUGIN_FLAGS = -nostdlib -shared -module

if HAVE_PAM
pam_lib = pam_slurm.la
else
pam_lib =
endif

pkglib_LTLIBRARIES = $(pam_lib)

if HAVE_PAM
pam_slurm_la_SOURCES = 				\
	pam_slurm.c				\
	$(top_builddir)/src/common/hostlist.c	\
	$(top_builddir)/src/common/hostlist.h
pam_slurm_la_LDFLAGS = $(PLUGIN_FLAGS)
else
EXTRA_pam_slurm_la_SOURCES = pam_slurm.c
endif

# Compiles fine
# Link line bad:
# gcc -shared  .libs/pam_slurm.o .libs/hostlist.o      -Wl,-soname -Wl,pam_slurm.so.0 -o .libs/pam_slurm.so.0.0.0
#.libs/pam_slurm.o: In function `_init':
# pam_slurm.c:(.text+0xa33): multiple definition of `_init'
# /usr/lib/gcc/x86_64-linux-gnu/4.3.3/../../../../lib/crti.o:/build/buildd/glibc-2.9/build-tree/amd64-libc/csu/crti.S:25: first defined here
# .libs/pam_slurm.o: In function `_fini':
# pam_slurm.c:(.text+0xa87): multiple definition of `_fini'
# /usr/lib/gcc/x86_64-linux-gnu/4.3.3/../../../../lib/crti.o:/build/buildd/glibc-2.9/build-tree/amd64-libc/csu/crti.S:37: first defined here
# collect2: ld returned 1 exit status

# Simple link is fine
# gcc -shared -nostdlib .libs/pam_slurm.o .libs/hostlist.o      -Wl,-soname -Wl,pam_slurm.so.0 -o .libs/pam_slurm.so.0.0.0



##
# $Id$
##

SLURM=		/opt/freeware
#SLURM=		/g/g0/morrone/local/blue
PROJECT=	slurm_ll_api

# The rpm build will override SLURM_LL_API_VERSION with the real version
# number from the META file.
SLURM_LL_API_VERSION=-1

LL_MODULE=	$(PROJECT).so
LL_MODULE_DIR=	$(SLURM)/lib

noinst_HEADERS=	

CC=		gcc
CFLAGS=		-Wall -O2 -fPIC -I$(SLURM)/include -g

CPPFLAGS=	-D_THREAD_SAFE -DWITH_PTHREADS -D'SLURM_LL_API_VERSION="$(SLURM_LL_API_VERSION)"'
LDFLAGS=	-Wl,-bM:SRE -Wl,-brtl -L$(SLURM)/lib -L/usr/lpp/ppe.poe/lib/threads /usr/lib/libntbl.a


LIBS=           -lc -lslurm -lpthreads
COPTS=		$(CFLAGS) $(CPPFLAGS)
INSTALL=	/usr/bin/install -c

HEADERS =	common.h		\
		config.h 		\
		hostlist.h		\
		list.h			\
		msg_thread.h	        \
		llapi.h			
SOURCES =       common.c		\
		hostlist.c		\
		list.c			\
		msg_thread.c	        \
		ll_ckpt_complete.c	\
		ll_close.c		\
		ll_deallocate.c		\
		ll_deallocate_job.c	\
		ll_event.c		\
		ll_fetch.c		\
		ll_get_data.c		\
		ll_get_job.c		\
		ll_get_objs.c		\
		ll_init_job.c		\
		ll_local_ckpt_complete.c\
		ll_local_ckpt_start.c	\
		ll_no_op.c		\
		ll_parse_string.c	\
		ll_query.c		\
		ll_request.c		\
		ll_set_data.c		\
		ll_set_request.c	\
		ll_spawn.c		\
		ll_spawn_task.c		\
		ll_spawn_connect.c	\
		ll_version.c
OBJECTS :=	$(SOURCES:.c=.o)

all: $(LL_MODULE)

$(LL_MODULE): $(OBJECTS)
	 $(CC) -nostdlib -shared -o $@ $(OBJECTS) $(LDFLAGS) $(LIBS)

install: $(LL_MODULE)
	mkdir -m 755 -p $(LL_MODULE_DIR)
	$(INSTALL) -m 755 $(LL_MODULE) $(LL_MODULE_DIR)

%.o:	%.c $(HEADERS)
	$(CC) $(COPTS) -c $<

hostlist: hostlist.c
#	NOTE: "#define TEST_MAIN 1" in hostlist.c
	$(CC) -g -o hostlist hostlist.c

test: test.c pmdv3test.c $(LL_MODULE)
#	NOTE: To emulate POE, don't build with "-brtl"
	$(CC) -o test -g -Wall -O2 test.c
	$(CC) -o pmdv3test pmdv3test.c

clean:
	-rm -f *.o *.so *~ \#* .\#* cscope*.out core core.* *.core tags TAGS

realclean: clean
	-rm -f *.tgz *.rpm

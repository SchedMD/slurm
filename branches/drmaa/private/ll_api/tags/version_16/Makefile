##
# $Id$
##

SLURM=		/admin/llnl
#SLURM=		/g/g0/morrone/local/blue
PROJECT=	slurm_ll_api

LL_MODULE=	$(PROJECT).so
LL_MODULE_DIR=	$(SLURM)/lib

noinst_HEADERS=	

CC=		gcc
CFLAGS=		-Wall -O2 -fPIC -I$(SLURM)/include -g

CPPFLAGS=	-D_THREAD_SAFE
LDFLAGS=	-Wl,-bM:SRE -Wl,-brtl -L$(SLURM)/lib -L/usr/local/openssl-0.9.7e_aix-5.2/lib -L/usr/lpp/ppe.poe/lib/threads -lcrypto /usr/lib/libntbl.a


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

test: test.c pmdv3test.c $(LL_MODULE)
#	NOTE: To emulate POE, don't build with "-brtl"
	$(CC) -o test -g -Wall -O2 test.c
	$(CC) -o pmdv3test pmdv3test.c

clean:
	-rm -f *.o *.so *~ \#* .\#* cscope*.out core core.* *.core tags TAGS

realclean: clean
	-rm -f *.tgz *.rpm

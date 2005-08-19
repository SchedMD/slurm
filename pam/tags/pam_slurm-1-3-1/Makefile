##
# $Id$
##


PROJECT=	pam_slurm

PAM_MODULE=	$(PROJECT).so
PAM_MODULE_DIR=	/lib/security

noinst_HEADERS=	hostlist.h

CC=		gcc
CFLAGS=		-Wall -O2
CPPFLAGS=	
DEFAULT_INCLUDES = -I.
DEFS=
LDFLAGS=	
LIBS=           
COPTS=		$(CFLAGS) $(CPPFLAGS) $(DEFS)
INSTALL=	/usr/bin/install -c
OBJECTS =	pam_slurm.o hostlist.o

all: $(PAM_MODULE) tags

$(PAM_MODULE): $(OBJECTS)

pam_slurm.o: pam_slurm.c $(noinst_HEADERS)
	$(CC) $(COPTS) -fPIC -c $<

hostlist.o: hostlist.c $(noinst_HEADERS)
	$(CC) $(COPTS) -fPIC -c $<

install: $(PAM_MODULE)
	mkdir -m 755 -p $(DESTDIR)$(PAM_MODULE_DIR)
	$(INSTALL) -m 755 $(PAM_MODULE) $(DESTDIR)$(PAM_MODULE_DIR)/

clean:
	-rm -f *.o *.so *~ \#* .\#* cscope*.out core core.* *.core tags TAGS

realclean: clean
	-rm -f *.tgz *.rpm

tags: *.[ch]
	-ctags *.[ch] 2>/dev/null

.SUFFIXES:
.SUFFIXES: .c .o .so

.o.so:
	$(CC) -nostdlib -shared -o $*.so $(OBJECTS) $(LIBS)

include Make-rpm.mk

##
# $Id$
##


PROJECT=	slurm_ll_api

LL_MODULE=	$(PROJECT).so
LL_MODULE_DIR=	/lib

noinst_HEADERS=	

CC=		gcc
CFLAGS=		-Wall -O2
CPPFLAGS=	
DEFAULT_INCLUDES = -I.
DEFS=
LDFLAGS=	
LIBS=           -lc -lslurm
COPTS=		$(CFLAGS) $(CPPFLAGS) $(DEFS)
INSTALL=	/usr/bin/install -c
OBJECTS =	ll_get_data.o

all: $(LL_MODULE)

$(LL_MODULE): $(OBJECTS)
	 $(CC) -nostdlib -shared -o $*.so $(OBJECTS) $(LIBS)

ll_get_data.o: ll_get_data.c $(noinst_HEADERS)
	$(CC) $(COPTS) -fPIC -c $<

install: $(LL_MODULE)
	mkdir -m 755 -p $(DESTDIR)$(LL_MODULE_DIR)
	$(INSTALL) -m 755 $(LL_MODULE) $(DESTDIR)$(LL_MODULE_DIR)/

clean:
	-rm -f *.o *.so *~ \#* .\#* cscope*.out core core.* *.core tags TAGS

realclean: clean
	-rm -f *.tgz *.rpm

.SUFFIXES:
.SUFFIXES: .c .o .so

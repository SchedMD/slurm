# Makefile for common library 

#
if DEBUG_MODULES
test_modules = controller node_mgr partition_mgr read_config 
else
test_modules = 
endif

AUTOMAKE_OPTIONS = foreign

noinst_LIBRARIES = libcommon.a

libcommon_a_SOURCES = bits_bytes.c list.c

noinst_HEADERS = list.h slurm.h

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#bits_bytes_CFLAGS     = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
# Note: automake 1.5 will complain about these...
#
bits_bytes     : bits_bytes_d.o

% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 

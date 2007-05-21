.SUFFIXES:
.SUFFIXES: .h .c .C .o

.c.o:
	xlc $(CFLAGS) -c $<

.C.o:	
	xlC $(CFLAGS) -c $<

ARCH     = aix
RM       = /usr/bin/rm

TRACE_HOME     = /u/noibm04/js4_slog2_ibm5
TRACE_INCDIR   = $(TRACE_HOME)/src/logformat/trace
TRACE_SRCDIR   = $(TRACE_HOME)/src/logformat/trace
TRACE_LIBDIR   = ../lib
TRACE_BINDIR   = ../bin

CSRC     = $(TRACE_SRCDIR)/logformat_trace_InputLog.c \
           ute_input.c ute2slog2_map.c
CSRC4T   = $(TRACE_SRCDIR)/trace_print.c \
           ute_input.c ute2slog2_map.c 
CPPSRC 	 = match.C

OBJ      = $(CSRC:.c=.o)
OBJ4T    = $(CSRC4T:.c=.o)
CPPOBJ   = $(CPPSRC:.C=.o)

CFLAGS     = -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/$(ARCH) \
             -I$(TRACE_INCDIR) $(COPTS) -D_LARGE_FILES #-D_DEBUG_TRACE_API

LDFLAGS    = -shared

MEMLIBS    = #-lefence

all:	$(TRACE_LIBDIR)/libTraceInput.a $(TRACE_BINDIR)/trace_print.exe

$(TRACE_LIBDIR)/libTraceInput.a: $(OBJ) $(CPPOBJ)
	xlC -bE:JavaTraceInput.exp -bnoentry -o $@ $(OBJ) $(CPPOBJ) \
		-L. -luteutil -lm #-lefence
	chmod 600 $@

$(TRACE_BINDIR)/trace_print.exe: $(OBJ4T) $(CPPOBJ)
	xlC -o $@ $(OBJ4T) $(CPPOBJ) $(MEMLIBS) \
		-L. -luteutil -lm #-lefence

clean:
	$(RM) -f *.o

realclean: clean
	$(RM) -f $(TRACE_LIBDIR)/libTraceInput.a $(TRACE_BINDIR)/trace_print.exe

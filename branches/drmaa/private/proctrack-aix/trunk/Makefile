TARGETS	= proctrack proctrackext proctrack_version proctrack_list \
          proctest64 proctestkill64 proctest32 proctestkill32

# The rpm build will override PROCTRACK_VERSION with the real version
# number from the META file.
PROCTRACK_VERSION=-1

CC	= xlc
CFLAGS	= -g -q64 -lcfg
CFLAGS32= -g -q32 -lcfg

default:	$(TARGETS)

proctrackext:	proctrackext.c proctrack.h proctrack_loader.h
	$(CC) -c $(CFLAGS) -DKERNEL -D'PROCTRACK_VERSION="$(PROCTRACK_VERSION)"' proctrackext.c -I.
	$(LD) -b64 -bE:proctrackext.exp -bI:/lib/kernex.exp -bloadmap:proctrackext.map \
		-lcsys -lsys -eproctrackext proctrackext.o -o proctrackext

proctrack:	proctrack.c proctrack.h proctrack_loader.h
	$(CC) -o proctrack $(CFLAGS) -DKERNEL proctrack.c

proctrack_version:	proctrack_version.c proctrack.h
	$(CC) -o proctrack_version -bI:proctrackext.exp $(CFLAGS) -DKERNEL proctrack_version.c

proctrack_list: proctrack_list.c proctrack.h
	$(CC) -o proctrack_list -bI:proctrackext.exp $(CFLAGS) -DKERNEL proctrack_list.c

proctest64:	proctest.c proctrack.h
	$(CC) -o proctest64 -bI:proctrackext.exp $(CFLAGS) -DKERNEL proctest.c

proctestkill64:	proctestkill.c proctrack.h
	$(CC) -o proctestkill64 -bI:proctrackext.exp $(CFLAGS) -DKERNEL proctestkill.c

proctest32:	proctest.c proctrack.h
	$(CC) -o proctest32 -bI:proctrackext.exp $(CFLAGS32) -DKERNEL proctest.c

proctestkill32:	proctestkill.c proctrack.h
	$(CC) -o proctestkill32 -bI:proctrackext.exp $(CFLAGS32) -DKERNEL proctestkill.c

clean:
	rm -f $(TARGETS) *.o *.map

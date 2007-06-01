# Process this file with automake to generate a Makefile.in
# Automake controls
AUTOMAKE_OPTIONS = foreign no-installinfo no-texinfo.tex
ACLOCAL_AMFLAGS = -I config/confdb
# Use AM_xFLAGS to modify compiler behavior
AM_CFLAGS=${COPTIONS}
# Package Description start here
bin_PROGRAMS = mpptest goptest buflimit 
EXTRA_PROGRAMS = tunepkt pktuse copytest vectest stress ctest cluster tcomm
EXTRA_SCRIPTS = rungoptest runmpptest runsample
noinst_HEADERS = mpptest.h getopts.h

# Some versions of automake can't handle continuation lines, so the line
# for extra_SOURCES must be a single line.
extra_SOURCES = gopf.c grph.c ovlap.c pair.c pattern.c getopts.c rate.c mpe_seq.c copy.c
mpptest_SOURCES = mpptest.c $(extra_SOURCES) halo.c
goptest_SOURCES = goptest.c $(extra_SOURCES)
vectest_SOURCES = vectest.f
stress_SOURCES = stress.c daytime.c getopts.c
tcomm_SOURCES = tcomm.c mpe_seq.c getopts.c
copytest_SOURCES = copytest.c duff.c

# Distribution modifications
EXTRA_DIST = config ${EXTRA_SCRIPTS}
#
# Autoconf/make oftens find the wrong versions of autoconf/make/header/local
dist-hook:
	-find ${distdir} -name CVS -print | sed 's/^/rm -rf /g' | sh
	cd ${distdir} ; sed -e 's/^AUTOMAKE.*/AUTOMAKE = true/g' \
	    -e 's/^AUTOCONF *=.*/AUTOCONF = true/g' \
	    -e 's/^AUTOHEADER *=.*/AUTOHEADER = true/g' \
	    -e 's/^ACLOCAL *=.*/ACLOCAL = true/g' Makefile.in > .tmp ; \
	rm -f Makefile.in ; \
	mv .tmp Makefile.in
distclean-local:
	rm -f config.system
clean-local:
	rm -f ${EXTRA_PROGRAMS}
	rm -f work.pc work.pcl

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = auxdir etc doc

# don't include lib and doc 'til we have something there
#SUBDIRS = lib src doc  

SUBDIRS = src testsuite

# remove *all* files that can be regenerated from automated tools
squeaky-clean: maintainer-clean
	@find . -name Makefile.in | xargs -t rm 
	rm -f config.h.in stamp-h.in configure aclocal.m4

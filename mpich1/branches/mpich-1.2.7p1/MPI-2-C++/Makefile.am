# -*- makefile -*-

AUTOMAKE_OPTIONS	= foreign dist-tarZ no-dependencies

SUBDIRS		= src balky
DIST_SUBDIRS	= $(SUBDIRS) contrib

## Note that insertlic.csh and license.hdr are only here so that
## our distscript can run them to insert the license headers.  They
## will be removed before the final tarball is made.

EXTRA_DIST	= HISTORY INSTALL LICENSE README VERSION \
		  distscript.csh insertlic.csh license.hdr \
		  Makefile.depend

examples:
	(cd contrib; $(MAKE) all)

dist-hook:
	(cd $(distdir); csh -f ./distscript.csh; rm -f distscript.csh TODO)

clean-local:
	@ echo "Making clean in contrib"
	@ (cd contrib; $(MAKE) clean)

distclean-local:
	@ echo "Making distclean in contrib"
	@ (cd contrib; $(MAKE) distclean)

SOURCEFILES	=
include $(top_srcdir)/Makefile.depend

 AUTOMAKE_OPTIONS = foreign
# copied from pidgin
#
perl_dir = perl
perlpath = /usr/bin/perl
perl_sources = \
	       $(perl_dir)/Makefile.PL.in \
	       $(perl_dir)/ppport.h \
	       $(perl_dir)/Slurm.xs \
	       $(perl_dir)/lib/Slurm.pm \
	       $(perl_dir)/lib/Slurm/Bitstr.pm \
	       $(perl_dir)/lib/Slurm/Constant.pm \
	       $(perl_dir)/lib/Slurm/Hostlist.pm \
	       $(perl_dir)/typemap \
	       $(perl_dir)/classmap \
	       $(perl_dir)/slurm-perl.h \
	       $(perl_dir)/alloc.c \
	       $(perl_dir)/conf.c \
	       $(perl_dir)/job.c \
	       $(perl_dir)/node.c \
	       $(perl_dir)/partition.c \
	       $(perl_dir)/reservation.c \
	       $(perl_dir)/step.c \
	       $(perl_dir)/step_ctx.c \
	       $(perl_dir)/topo.c \
	       $(perl_dir)/trigger.c
test_sources = \
	$(perl_dir)/t/00-use.t \
	$(perl_dir)/t/01-error.t \
	$(perl_dir)/t/02-string.t \
	$(perl_dir)/t/04-alloc.c \
	$(perl_dir)/t/05-signal.t \
	$(perl_dir)/t/06-complete.t \
	$(perl_dir)/t/07-spawn.t \
	$(perl_dir)/t/08-conf.t \
	$(perl_dir)/t/09-resource.t \
	$(perl_dir)/t/10-job.t \
	$(perl_dir)/t/11-step.t \
	$(perl_dir)/t/12-node.t \
	$(perl_dir)/t/13-topo.t \
	$(perl_dir)/t/14-select.t \
	$(perl_dir)/t/15-partition.t \
	$(perl_dir)/t/16-reservation.t \
	$(perl_dir)/t/17-ping.t \
	$(perl_dir)/t/18-suspend.t \
	$(perl_dir)/t/20-trigger.t \
	$(perl_dir)/t/21-hostlist.t \
	$(perl_dir)/t/22-list.t \
	$(perl_dir)/t/23-bitstr.t

$(perl_dir)/Makefile:	$(perl_dir)/Makefile.PL
	@if test "x${top_srcdir}" != "x${top_builddir}"; then \
		for f in ${perl_sources}; do \
			$(mkdir_p) `dirname $$f`; \
			${LN_S} -f ${abs_srcdir}/$$f $$f; \
		done; \
		for f in ${test_sources}; do \
			$(mkdir_p) `dirname $$f`; \
			${LN_S} -f ${abs_srcdir}/$$f $$f; \
		done; \
	fi
	@cd $(perl_dir) && $(perlpath) Makefile.PL $(PERL_MM_PARAMS) prefix=${prefix} INSTALL_BASE= PERL_MM_OPT= INSTALLSITEMAN1DIR=$(mandir)/man1 INSTALLSITEMAN3DIR=$(mandir)/man3

#
# Note on linking logic below
#
# Install at PREFIX and ignore both INSTALL_BASE and PERL_MM_OPT. Having both
# more than one installation location specification results in a build error.
# Suse Linux compiles with gcc, but picks some other compiler to use for linking.
#   Since some CFLAGS may be incompatible with this other compiler, the build
#   may fail, as seen on BlueGene platforms.
# Other Linux implementations sems to work fine with the LD specified as below
#
all-local: $(perl_dir)/Makefile #libslurm
	@cd $(perl_dir) && \
	if [ ! -f Makefile ]; then \
		$(perlpath) Makefile.PL $(PERL_MM_PARAMS) prefix=${prefix} INSTALL_BASE= PERL_MM_OPT= INSTALLSITEMAN1DIR=$(mandir)/man1 INSTALLSITEMAN3DIR=$(mandir)/man3; \
	fi && \
	($(MAKE) CC="$(CC)" LD="$(CC) $(CFLAGS) $(LDFLAGS)" CCFLAGS="$(PERL_CFLAGS) -g -static $(CFLAGS) $(CPPFLAGS)" $(PERL_EXTRA_OPTS) || \
	 $(MAKE) CC="$(CC)" LD="$(CC) $(CFLAGS) $(LDFLAGS)" CCFLAGS="$(PERL_CFLAGS) -g -static $(CFLAGS) $(CPPFLAGS)" $(PERL_EXTRA_OPTS)) && \
	cd ..;

install-exec-local:
	@cd $(perl_dir) && \
	$(MAKE) DESTDIR=$(DESTDIR) install && \
	cd ..;

# Evil Hack (TM)
# ... which doesn't work with DESTDIR installs. FIXME?
uninstall-local:
	@cd $(perl_dir) && \
	`$(MAKE) uninstall | grep unlink | sed -e 's#/usr#${prefix}#' -e 's#unlink#rm -f#'` && \
	cd ..;

clean-generic:
	@cd $(perl_dir); \
	$(MAKE) clean; \
	if test "x${top_srcdir}" != "x${top_builddir}"; then \
		rm -fr lib t *c *h *xs typemap classmap; \
	fi; \
	cd ..;

	@if test "x${top_srcdir}" != "x${top_builddir}"; then \
		for f in ${perl_sources}; do \
			$(mkdir_p) `dirname $$f`; \
			${LN_S} -f ${abs_srcdir}/$$f $$f; \
		done; \
		for f in ${test_sources}; do \
			$(mkdir_p) `dirname $$f`; \
			${LN_S} -f ${abs_srcdir}/$$f $$f; \
		done; \
	fi

distclean-generic:
	@cd $(perl_dir); \
	$(MAKE) realclean; \
	rm -f Makefile.PL; \
	rm -f Makefile.old; \
	rm -f Makefile; \
	cd ..;

	@rm -f Makefile

	@if test "x${top_srcdir}" != "x${top_builddir}"; then \
		for f in ${perl_sources}; do \
			$(mkdir_p) `dirname $$f`; \
			${LN_S} -f ${abs_srcdir}/$$f $$f; \
		done; \
		for f in ${test_sources}; do \
			$(mkdir_p) `dirname $$f`; \
			${LN_S} -f ${abs_srcdir}/$$f $$f; \
		done; \
	fi

AM_CPPFLAGS = \
	-DVERSION=\"$(VERSION)\" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(DEBUG_CFLAGS) \
	$(PERL_CFLAGS)

htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

man5_MANS = \
	acct_gather.conf.5 \
	burst_buffer.conf.5 \
	cgroup.conf.5 \
	gres.conf.5 \
	helpers.conf.5 \
	job_container.conf.5 \
	mpi.conf.5 \
	namespace.yaml.5 \
	oci.conf.5 \
	resources.yaml.5 \
	slurm.conf.5 \
	slurmdbd.conf.5 \
	topology.conf.5 \
	topology.yaml.5

if HAVE_MAN2HTML

generated_html = \
	acct_gather.conf.html \
	burst_buffer.conf.html \
	cgroup.conf.html \
	gres.conf.html \
	helpers.conf.html \
	job_container.conf.html \
	mpi.conf.html \
	namespace.yaml.html \
	oci.conf.html \
	resources.yaml.html \
	slurm.conf.html \
	slurmdbd.conf.html \
	topology.conf.html \
	topology.yaml.html

# Generated HTML man pages are kept out of html_DATA so that `make` (and
# `make install`) do not rebuild them. Use `make html` to build and
# `make install-html` to install.
html-local: ${generated_html}

install-html-local: ${generated_html}
	@$(NORMAL_INSTALL)
	test -z "$(htmldir)" || $(MKDIR_P) "$(DESTDIR)$(htmldir)"
	@list='$(generated_html)'; for f in $$list; do \
	  echo " $(INSTALL_DATA) $$f '$(DESTDIR)$(htmldir)'"; \
	  $(INSTALL_DATA) "$$f" "$(DESTDIR)$(htmldir)" || exit $$?; \
	done

uninstall-local:
	@$(NORMAL_UNINSTALL)
	@list='$(generated_html)'; for f in $$list; do \
	  echo " rm -f '$(DESTDIR)$(htmldir)/$$f'"; \
	  rm -f "$(DESTDIR)$(htmldir)/$$f"; \
	done

MOSTLYCLEANFILES = ${generated_html}

SUFFIXES = .html

.5.html:
	`dirname $<`/../man2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif

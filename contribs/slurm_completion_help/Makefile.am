#
# Makefile for slurm completion help scripts

AUTOMAKE_OPTIONS = foreign

bashcomp_DATA = slurm_completion.sh
bashcompdir = $(datadir)/bash-completion/completions
bashcompbin := \
	sacct \
	sacctmgr \
	salloc \
	sattach \
	sbatch \
	sbcast \
	scancel \
	scontrol \
	scrontab \
	sdiag \
	sinfo \
	slurmrestd \
	sprio \
	squeue \
	sreport \
	srun \
	sshare \
	sstat \
	strigger

install-bashcompDATA: $(bashcomp_DATA)
	@$(NORMAL_INSTALL)
	test -z "$(DESTDIR)$(bashcompdir)" || $(mkdir_p) "$(DESTDIR)$(bashcompdir)"
	install -v -m 644 "$(top_srcdir)/contribs/slurm_completion_help/$(bashcomp_DATA)" "$(DESTDIR)$(bashcompdir)/$(bashcomp_DATA)" || true
	@list='$(bashcompbin)'; for p in $$list; do \
	  ln -vsf "$(DESTDIR)$(bashcompdir)/$(bashcomp_DATA)" "$(DESTDIR)$(bashcompdir)/$$p" || true; \
	  done

uninstall-bashcompDATA:
	@$(NORMAL_UNINSTALL)
	rm -vf "$(DESTDIR)$(bashcompdir)/$(bashcomp_DATA)" || true
	@list='$(bashcompbin)'; for p in $$list; do \
	  rm -vf "$(DESTDIR)$(bashcompdir)/$$p" || true; \
	done

clean:

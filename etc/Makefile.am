ETC_FILES = init.d.slurmdbd init.d.slurm \
	    slurmctld.service slurmdbd.service slurmd.service

CLEANFILES = $(ETC_FILES)

edit = sed \
        -e 's|@bindir[@]|$(bindir)|g' \
        -e 's|@libdir[@]|$(libdir)|g' \
        -e 's|@sbindir[@]|$(sbindir)|g' \
        -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@SYSTEMD_TASKSMAX_OPTION[@]|$(SYSTEMD_TASKSMAX_OPTION)|g'

noinst_DATA = $(ETC_FILES)

init.d.slurmdbd: Makefile $(srcdir)/init.d.slurmdbd.in
	$(edit) $(srcdir)/init.d.slurmdbd.in > init.d.slurmdbd

init.d.slurm: Makefile $(srcdir)/init.d.slurm.in
	$(edit) $(srcdir)/init.d.slurm.in > init.d.slurm

slurmctld.service: Makefile $(srcdir)/slurmctld.service.in
	$(edit) $(srcdir)/slurmctld.service.in > slurmctld.service

slurmdbd.service: Makefile $(srcdir)/slurmdbd.service.in
	$(edit) $(srcdir)/slurmdbd.service.in > slurmdbd.service

slurmd.service: Makefile $(srcdir)/slurmd.service.in
	$(edit) $(srcdir)/slurmd.service.in > slurmd.service

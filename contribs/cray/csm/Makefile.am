#
# Makefile for cray/csm scripts
#

EXTRA_DIST = \
	gres.conf.j2			\
	slurm.conf.j2			\
	slurmconfgen_smw.py		\
	slurm_playbook.yaml.in

noinst_DATA = slurm_playbook.yaml

slurm_playbook.yaml: slurm_playbook.yaml.in Makefile
	sed -e 's|@prefix[@]|$(prefix)|g' \
	    -e 's|@bindir[@]|$(bindir)|g' \
	    -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	    ${abs_srcdir}/slurm_playbook.yaml.in >slurm_playbook.yaml

clean-generic:
	rm -f slurm_playbook.yaml

# $Id: Makefile.am 7041 2006-01-17 16:36:04Z jette $

htmldir = ${prefix}/share/doc/@PACKAGE@-@VERSION@/html

generated_html = \
	api.html \
	authplugins.html \
	big_sys.html \
	bluegene.html \
	checkpoint_plugins.html \
	cons_res.html \
	documentation.html \
	download.html \
	faq.html \
	help.html \
	ibm.html \
	jobacctplugins.html \
	jobcompplugins.html \
	mail.html \
	maui.html \
	moab.html \
	mpiplugins.html \
	news.html \
	overview.html \
	platforms.html \
	plugins.html \
	proctrack_plugins.html \
	programmer_guide.html \
	publications.html \
	quickstart_admin.html \
	quickstart.html \
	schedplugins.html \
	selectplugins.html \
	slurm.html \
	switchplugins.html \
 	team.html \
 	testimonials.html

html_DATA = \
	${generated_html} \
	arch.gif \
	configurator.html \
	coding_style.pdf \
	entities.gif \
	lll.gif \
	slurm_banner_filler.jpg \
	slurm_banner_left.jpg \
	slurm_banner_right.jpg \
	slurm_design.pdf \
	slurmstyles.css

MOSTLYCLEANFILES = ${generated_html}

EXTRA_DIST = $(html_DATA)

SUFFIXES = .html

.shtml.html:
	`dirname $<`/shtml2html.py $<

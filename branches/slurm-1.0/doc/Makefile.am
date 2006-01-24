# $Id$

SUBDIRS = man 

htmldir = ${prefix}/share/doc/@PACKAGE@-@VERSION@/html

generated_html = \
	html/api.html \
	html/authplugins.html \
	html/big_sys.html \
	html/bluegene.html \
	html/checkpoint_plugins.html \
	html/cons_res.html \
	html/documentation.html \
	html/download.html \
	html/faq.html \
	html/help.html \
	html/ibm.html \
	html/jobacctplugins.html \
	html/jobcompplugins.html \
	html/mail.html \
	html/maui.html \
	html/mpiplugins.html \
	html/news.html \
	html/overview.html \
	html/platforms.html \
	html/plugins.html \
	html/programmer_guide.html \
	html/publications.html \
	html/quickstart_admin.html \
	html/quickstart.html \
	html/schedplugins.html \
	html/selectplugins.html \
	html/slurm.html \
	html/switchplugins.html \
	html/team.html \
	html/testimonials.html

html_DATA = \
	${generated_html} \
	html/arch.gif \
	html/configurator.html \
	html/coding_style.pdf \
	html/entities.gif \
	html/lll.gif \
	html/slurm_banner_filler.jpg \
	html/slurm_banner_left.jpg \
	html/slurm_banner_right.jpg \
	html/slurm_design.pdf \
	html/slurmstyles.css

MOSTLYCLEANFILES = ${generated_html}

EXTRA_DIST = \
	pubdesign/Makefile \
	pubdesign/report.tex \
	pubdesign/sample.config \
	figures \
	common \
	$(html_DATA)

SUFFIXES = .html

.shtml.html:
	cd html && ./shtml2html.py `basename $<`

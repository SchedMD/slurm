# The following comments are to remind me how the automatic variables work:
# $@ - target
# $% - target member
# $< - First prerequisite
# $? - All (newer) prerequisites
# $^ - All prerequisites
# $+ - $^ but with repetitions
# $* - $* stem of pattern (for "foo.c" in %.c:%.o this would be "foo")
# 'info "GNU make"': "Using variables": "Automatic" also lists a few more.

REPORT = report

TEX = ../common/llnlCoverPage.tex $(REPORT).tex

FIGDIR = ../figures
FIGS = $(FIGDIR)/arch2.eps \
       $(FIGDIR)/comm.eps \
       $(FIGDIR)/entities2.eps \
       $(FIGDIR)/launch.eps

BIB = ../common/project.bib

%.eps: %.dia
	dia --nosplash -e $@ $<
%.eps: %.gpl
	gnuplot $<
%.eps: %.fig
	fig2dev -Lps $< $@
%.eps: %.obj
	tgif -print -eps $<
%.ps: %.dvi
	dvips -K -t letter -o $(@F) $(<F)
%.pdf: %.dvi
	dvipdf $< $@

all: $(REPORT).ps


$(REPORT).dvi: $(TEX) $(FIGS) $(BIB)
	rm -f *.log *.aux *.blg *.bbl
	(TEXINPUTS=.:../common::; export TEXINPUTS; \
	 BIBINPUTS=.:../common::; export BIBINPUTS; \
	 latex $(REPORT); \
	 bibtex $(REPORT); \
	 latex $(REPORT); \
	 latex $(REPORT) )

view: $(REPORT).ps
	ghostview $(REPORT) &

clean:
	rm -f *~ *.dvi *.log *.aux *.ps *.pdf *.blg *.bbl #*.eps #*.gif


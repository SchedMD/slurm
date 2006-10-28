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
FIGS = $(FIGDIR)/allocate-init.eps \
       $(FIGDIR)/arch.eps \
       $(FIGDIR)/connections.eps \
       $(FIGDIR)/entities.eps \
       $(FIGDIR)/interactive-job-init.eps \
       $(FIGDIR)/queued-job-init.eps \
       $(FIGDIR)/slurm-arch.eps \
       $(FIGDIR)/times.eps

PLOTS = $(FIGDIR)/times.eps 

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


$(REPORT).dvi: $(TEX) $(FIGS) $(PLOTS) $(BIB)
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
	rm -f *~ *.dvi *.log *.aux $(REPORT).ps *.blg *.bbl #*.eps #*.gif *.ps
	      

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

TEX = llnl.tex $(REPORT).tex
FIGS = arch.eps slurm-arch.eps
PLOTS = 
BIB = project.bib

%.eps: %.dia
	dia --nosplash -e $@ $< 
%.eps: %.gnuplot %.data
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
	latex llnl
	bibtex llnl
	latex llnl
	latex llnl
	mv llnl.dvi $(REPORT).dvi

view: $(REPORT).ps
	ghostview $(REPORT) &

clean: 
	rm -f *~ *.dvi *.log *.aux report.ps *.blg *.bbl #*.eps #*.gif

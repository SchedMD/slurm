all: slides.dvi

# -paper usr
view: slides.dvi
	xdvi -paper usr -expert -s 0 slides
viewps: slides.ps
	ghostview -seascape slides.ps

clean:
	rm -f slides.dvi slides.aux slides.log slides.ps

slides.dvi: slides.tex penguin.eps
	latex slides
slides.ps: slides.dvi
	dvips -O 0.75in,0 -f <slides.dvi >$@  # slides 1-per page

%.eps: %.obj
	tgif -print -eps $<

%.pdf: %.dvi
	dvipdf $< $@


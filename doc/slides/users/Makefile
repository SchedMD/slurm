all: slides

# -paper usr
clean:
	rm -f slides.dvi slides.aux slides.log slides.ps slides.pdf slides.out

slides: slides.tex
	(TEXINPUTS=.:./tex:./figures:../../common::; export TEXINPUTS; \
	 latex slides; latex slides; \
	 dvips -K -Ppdf -G0 slides.dvi -o slides.ps; \
	 ps2pdf13 -dPDFsettings=/prepress slides.ps slides.pdf )

all:
	latex users-guide.tex
	latex users-guide.tex
	latex users-guide.tex
	dvips -o users-guide.ps users-guide.dvi
	gzip -f users-guide.ps

bib:
	latex users-guide.tex
	bibtex users-guide
	latex users-guide.tex
	latex users-guide.tex
	dvips -o users-guide.ps users-guide.dvi

clean:
	-rm -f users-guide.toc users-guide.log users-guide.dvi \
	       users-guide.aux users-guide.bbl

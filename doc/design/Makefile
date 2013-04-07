BASE = sns_das_design
SRC = $(BASE).tex */*.tex intro.tex
BIBS = 
LATEX = pdflatex

all: $(BASE).pdf SNS_DAS_design.pdf

$(BASE).pdf: $(SRC) #$(BASE).bbl
	if [ -f $(BASE).toc ]; then \
		cp $(BASE).toc $(BASE).toc.old; \
	else \
		touch $(BASE).toc.old; \
	fi
	if [ -f $(BASE).aux ]; then \
		cp $(BASE).aux $(BASE).aux.old; \
	else \
		touch $(BASE).aux.old; \
	fi
	-svn info | awk '/^Revision:/ {print "Revision:", $$2 ","}' >revision.tex
	$(LATEX) $(BASE) | tee latex.out
	if ! cmp -s $(BASE).toc $(BASE).toc.old; then \
		touch .rebuild; \
	fi
	if [ -n "`cmp $(BASE).aux $(BASE).aux.old 2>&1`" -o \
	     -n "`grep '^LaTeX Warning: Citation.*undefined' latex.out`" ]; then \
		touch .rebuild; \
	fi
	while [ -f .rebuild -o \
		-n "`grep '^LaTeX Warning:.*Rerun' latex.out`" ]; do \
		rm -f .rebuild; \
		cp $(BASE).toc $(BASE).toc.old; \
		$(LATEX) $(BASE) | tee latex.out; \
		if ! cmp -s $(BASE).toc $(BASE).toc.old; then \
			touch .rebuild; \
		fi \
	done
	rm -f latex.out $(BASE).toc.old $(BASE).aux.old revision.tex

SNS_DAS_design.pdf: $(BASE).pdf
	cp $< $@
	if test "$$destdir" != "" ; then cp $< $$destdir/$@ ; fi

$(BASE).bbl: $(BIBS)
	-if [ -f $(BASE).aux ]; then bibtex $(BASE); fi

bibtex:
	-if [ -f $(BASE).aux ]; then bibtex $(BASE); fi

clean:
	rm -f $(BASE).bbl $(BASE).blg $(BASE).log $(BASE).toc $(BASE).toc.old \
	$(BASE).pdf $(BASE).aux $(BASE).aux.old $(BASE).out latex.out .rebuild \
	SNS_DAS_design.pdf revision.tex *.idx */*.idx */*.aux */*.bbl */*.blg \
	*/*.pdf */*.aux.old */*.old */*.log
	if test "$$destdir" != "" ; then rm -f $$destdir/sns_das_design.pdf ; fi


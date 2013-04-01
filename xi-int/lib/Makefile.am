CUDD      = cudd-2.4.2
Z3        = z3-x64-3.2
BOOLECTOR = boolector-1.4.1-376e6b0-110304
PICOSAT   = picosat-936
PRECOSAT  = precosat-570-239dbbe-100801

all-local: $(SMT_LIB)

libcudd.a:
	$(CURL) ftp://vlsi.colorado.edu/pub/$(CUDD).tar.gz | tar xz
	cd $(CUDD) && make XCFLAGS="-fPIC -mtune=native -DHAVE_IEEE_754 -imacros '$(abs_top_builddir)/config.h'"
	cd $(CUDD) && rm -rf .libs && mkdir .libs && cd .libs && \
		ar x ../mtr/libmtr.a   && \
		ar x ../st/libst.a     && \
		ar x ../util/libutil.a && \
		ar x ../epd/libepd.a   && \
		ar x ../cudd/libcudd.a && \
		rm tmpfile.o
	:;{ \
	  echo '#pragma once' && \
	  echo '#include "$(top_builddir)/config.h"' && \
	  echo '#include "$(CUDD)/include/util.h"' && \
	  echo '#include "$(CUDD)/include/cudd.h"'; \
	} > cudd.h~
	mv cudd.h~ cudd.h
	ar rcs $@ $(CUDD)/.libs/*.o
	ranlib $@

libboolector.a:
	$(CURL) 'http://fmv.jku.at/picosat/$(PICOSAT).tar.gz' | tar xz
	ln -f -s $(PICOSAT) picosat
	sed -i -e "s/CFLAGS=/CFLAGS=-fPIC /" picosat/makefile.in
	cd picosat && ./configure -O && make
	$(CURL) 'http://fmv.jku.at/precosat/$(PRECOSAT).tar.gz' | tar xz
	ln -f -s $(PRECOSAT) precosat
	sed -i -e "s/;m32=yes//g" precosat/configure
	sed -i -e "s/malloc.h/stdlib.h/g" precosat/precosat.cc
	echo "#include <stddef.h>" > precosat/precosat.hh~
	cat precosat/precosat.hh >> precosat/precosat.hh~
	mv -f precosat/precosat.hh~ precosat/precosat.hh
	sed -i -e "s/CXXFLAGS=/CXXFLAGS=-fPIC /" precosat/makefile.in
	cd precosat && ./configure -O && make precosat.o precobnr.o
	$(CURL) 'http://fmv.jku.at/boolector/$(BOOLECTOR).tar.gz' | tar xz
	ln -f -s $(BOOLECTOR) boolector
	sed -i -e "s/CFLAGS=/CFLAGS=-fPIC /" boolector/makefile.in
	cd boolector && ./configure -precosat && make
	ln -f -s boolector/libboolector.a

libz3.so:
	$(CURL) 'http://research.microsoft.com/projects/z3/$(Z3).tar.gz' | tar xz
	cp -f z3/include/*.h z3/lib/$@ .

all = cint gccint intsat vrint satlinux cintld
all-local: $(all)

cint: $(top_srcdir)/configure.ac Makefile.am
	:;{ \
	echo "#!/bin/bash" && \
	echo "clang -Xclang -load -Xclang '$(abs_builddir)/`$(srcdir)/dlname $(top_builddir)/src/libcint.la`' -Xclang -plugin -Xclang int -no-integrated-as -fsyntax-only" '$$@'; \
	} > $@~
	chmod a+x $@~
	mv $@~ $@

gccint: cint $(abs_srcdir)/gccint.frag
	:;{ \
	echo "#!/bin/bash" && \
	echo "CINT='$(abs_builddir)/cint'" && \
	echo "CINTARGS='-Qunused-arguments -w -mcmodel=kernel -I$(abs_top_srcdir)/tests/linux/include'" ; \
	} > $@~
	cat $(abs_srcdir)/gccint.frag >> $@~
	chmod a+x $@~
	mv $@~ $@

satlinux: $(abs_srcdir)/satlinux.frag
	:;{ \
	echo "#!/usr/bin/python" && \
	echo "satcmd='opt --disable-output -load=$(abs_builddir)/`$(srcdir)/dlname $(top_builddir)/src/libintsat.la` -trap-sat -ext=$(abs_builddir)/../tests/intrange.txt'"; \
	} > $@~
	cat $(abs_srcdir)/satlinux.frag >> $@~
	chmod a+x $@~
	mv $@~ $@

vrint: $(top_srcdir)/configure.ac Makefile.am
	:;{ \
	echo "#!/bin/bash" && \
	echo "opt --load '$(abs_builddir)/`$(srcdir)/dlname $(top_builddir)/src/libintrange.la`' --mem2reg --ranges --ranges-verbose-output " '$$@' "> /dev/null"; \
	} > $@~
	chmod a+x $@~
	mv $@~ $@

intsat: $(top_srcdir)/configure.ac Makefile.am
	:;{ \
	echo "#!/bin/bash" && \
	echo "opt --disable-output -load='$(abs_builddir)/`$(srcdir)/dlname $(top_builddir)/src/libintsat.la`' -trap-sat" '$$@'; \
	} > $@~
	chmod a+x $@~
	mv $@~ $@

intmatch: $(top_srcdir)/configure.ac Makefile.am
	:;{ \
	echo "#!/bin/bash" && \
	echo "clang" -I '$(abs_srcdir)/intmatch' '$$@' "-o - -emit-llvm -c -O0 -g | opt --disable-output -load='$(abs_builddir)/`$(srcdir)/dlname $(top_builddir)/src/libintmatch.la`' -targetlibinfo -tbaa -basicaa -globalopt -deadargelim -simplifycfg -prune-eh -functionattrs -argpromotion -scalarrepl-ssa -gvn -tautological-compare"; \
	} > $@~
	chmod a+x $@~
	mv $@~ $@

cintld: $(top_srcdir)/configure.ac Makefile.am
	cp -f $(top_builddir)/src/$@ $@

clean-local:
	rm -rf $(all)

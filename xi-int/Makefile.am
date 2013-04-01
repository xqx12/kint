SUBDIRS = lib src tools tests

.PHONY: linux

linux:
	$(MAKE) check TF="--jobs=`$(srcdir)/tools/ncpu` -k linux"

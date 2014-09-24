MODULE_big	= pltoolbox
OBJS		= strings.o date.o diff.o utils.o record.o bitmapset.o
DATA_built = pltoolbox.sql
DATA = uninstall_pltoolbox.sql pltoolbox--1.0.3.sql pltoolbox--unpackaged--1.0.3.sql
REGRESS = pltoolbox
EXTENSION = pltoolbox
DOCS = README.md

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pltoolbox
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

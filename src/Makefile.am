## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = no-dependencies

bin_PROGRAMS = normalize
bin_SCRIPTS = normalize-mp3
normalize_SOURCES = normalize.c riff.c version.c getopt.c getopt1.c getopt.h riff.h
man_MANS = normalize.1
EXTRA_DIST = $(man_MANS) normalize-mp3.in mktestwav.c
SUBDIRS = intl po
localedir = $(datadir)/locale
INCLUDES = -Iintl -DLOCALEDIR=\"$(localedir)\"
LDADD = @INTLLIBS@
DISTCLEANFILES = intl/libintl.h

# don't know why I need this, but I do...
AMTAR = tar

mktestwav: mktestwav.o riff.o version.o getopt.o getopt1.o
	$(CC) $(LDFLAGS) -o $@ $^ -lm

mktestwav.o: mktestwav.c riff.h

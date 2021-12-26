#
# Makefile for noad
#
#
BINDIR   = /usr/local/bin
TMPDIR = /tmp
NAME=noad
ARCHIVE = $(NAME)-$(VERSION)
PACKAGE = $(ARCHIVE)
VERSION = $(shell grep 'static const char \*VERSION *=' $(NAME).cpp | awk '{ print $$6 }' | sed -e 's/[";]//g')


OBJS = cchecklogo.o ccontrol.o cgetlogo.o ctoolbox.o noad.o noaddata.o\
            tools.o vdr_cl.o main.o

DEFINES += -D_GNU_SOURCE

INCLUDES = -I/usr/local/include/mpeg2dec

ifdef VFAT
# for people who want their video directory on a VFAT partition
DEFINES += -DVFAT
endif

all: noad

# Implicit rules:

%.o: %.cpp
	g++ -g -O2 -Wall -Woverloaded-virtual -c $(DEFINES) $(INCLUDES) $<

# Dependencies:
MAKEDEP = g++ -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.cpp) > $@

include $(DEPFILE)

# The main program:

noad: $(OBJS) $(DTVLIB)
	g++ -g -O2 $(OBJS) $(NCURSESLIB) -lmpeg2 $(LIBDIRS) -o noad

#	g++ -g -O2 $(OBJS) $(NCURSESLIB) -ljpeg -lpthread $(LIBDIRS) -o noad
# Install the files:

install:
	@cp noad $(BINDIR)

# Housekeeping:

clean:
	-rm -f $(OBJS) $(DEPFILE) noad *~ *.~*

dist: clean
	@-rm -rf $(PACKAGE).tgz
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) --exclude=*.tgz $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

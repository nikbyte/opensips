# example module makefile
#
# 
# WARNING: do not run this directly, it should be run by the master Makefile

include ../../Makefile.defs
auto_gen=
NAME=jabber.so

# extra debug messages
DEFS+=-DXJ_EXTRA_DEBUG
# -DHAVE_IHTTP

ifeq ($(CROSS_COMPILE),)
LIBS += -lexpat
else
# expat.h location
DEFS+=-I$(LOCALBASE)/include
LIBS += -L$(LOCALBASE)/lib -lexpat
endif

include ../../Makefile.modules

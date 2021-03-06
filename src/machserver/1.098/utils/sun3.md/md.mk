#
# Prototype Makefile for machine-dependent directories.
#
# A file of this form resides in each ".md" subdirectory of a
# command.  Its name is typically "md.mk".  During makes in the
# parent directory, this file (or a similar file in a sibling
# subdirectory) is included to define machine-specific things
# such as additional source and object files.
#
# This Makefile is automatically generated.
# DO NOT EDIT IT OR YOU MAY LOSE YOUR CHANGES.
#
# Generated from /sprite/lib/mkmf/Makefile.md
# Tue Jul  2 14:18:40 PDT 1991
#
# For more information, refer to the mkmf manual page.
#
# $Header: /sprite/lib/mkmf/RCS/Makefile.md,v 1.6 90/03/12 23:28:42 jhh Exp $
#
# Allow mkmf

SRCS		= sun3.md/dumpEvents.c dump.c hash.c trace.c traceLog.c
HDRS		= bf.h byte.h dump.h dumpInt.h hash.h sospRecord.h trace.h traceLog.h
MDPUBHDRS	= 
OBJS		= sun3.md/dump.o sun3.md/dumpEvents.o sun3.md/hash.o sun3.md/trace.o sun3.md/traceLog.o
CLEANOBJS	= sun3.md/dumpEvents.o sun3.md/dump.o sun3.md/hash.o sun3.md/trace.o sun3.md/traceLog.o
INSTFILES	= sun3.md/md.mk sun3.md/dependencies.mk Makefile local.mk tags TAGS
SACREDOBJS	= 

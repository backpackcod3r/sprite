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
# Thu Nov 21 15:27:26 PST 1991
#
# For more information, refer to the mkmf manual page.
#
# $Header: /sprite/lib/mkmf/RCS/Makefile.md,v 1.6 90/03/12 23:28:42 jhh Exp $
#
# Allow mkmf

SRCS		= acosh.c asincos.c asinh.c atan.c atanh.c cosh.c erf.c exp.c exp__E.c expm1.c fabs.c floor.c j0.c j1.c jn.c lgamma.c log.c log10.c log1p.c log__L.c pow.c sinh.c tanh.c atan2.c cabs.c cbrt.c sincos.c support.c tan.c fmod.c
HDRS		= math.h trig.h
MDPUBHDRS	= 
OBJS		= sun3.md/acosh.o sun3.md/asincos.o sun3.md/asinh.o sun3.md/atan.o sun3.md/atanh.o sun3.md/cosh.o sun3.md/erf.o sun3.md/exp.o sun3.md/exp__E.o sun3.md/expm1.o sun3.md/fabs.o sun3.md/floor.o sun3.md/j0.o sun3.md/j1.o sun3.md/jn.o sun3.md/lgamma.o sun3.md/log.o sun3.md/log10.o sun3.md/log1p.o sun3.md/log__L.o sun3.md/pow.o sun3.md/sinh.o sun3.md/tanh.o sun3.md/atan2.o sun3.md/cabs.o sun3.md/cbrt.o sun3.md/sincos.o sun3.md/support.o sun3.md/tan.o sun3.md/fmod.o
CLEANOBJS	= sun3.md/acosh.o sun3.md/asincos.o sun3.md/asinh.o sun3.md/atan.o sun3.md/atanh.o sun3.md/cosh.o sun3.md/erf.o sun3.md/exp.o sun3.md/exp__E.o sun3.md/expm1.o sun3.md/fabs.o sun3.md/floor.o sun3.md/j0.o sun3.md/j1.o sun3.md/jn.o sun3.md/lgamma.o sun3.md/log.o sun3.md/log10.o sun3.md/log1p.o sun3.md/log__L.o sun3.md/pow.o sun3.md/sinh.o sun3.md/tanh.o sun3.md/atan2.o sun3.md/cabs.o sun3.md/cbrt.o sun3.md/sincos.o sun3.md/support.o sun3.md/tan.o sun3.md/fmod.o
INSTFILES	= sun3.md/md.mk sun3.md/dependencies.mk Makefile local.mk tags TAGS
SACREDOBJS	= 

# Makefile based on BSD make.
# Our mk stubs also work with GNU make.
# Copyright 2008 Roy Marples <roy@marples.name>

PROG=		dhcpcd
SRCS=		common.c dhcp.c dhcpcd.c logger.c net.c signals.c
SRCS+=		configure.c client.c
SRCS+=		${SRC_IF} ${SRC_PF}

LIBEXECDIR?=	${PREFIX}/system/etc/dhcpcd
SCRIPT=		${LIBEXECDIR}/dhcpcd-run-hooks
HOOKDIR=	${LIBEXECDIR}/dhcpcd-hooks

BINDIR=		${PREFIX}/sbin
DBDIR=		/data/misc/dhcp
SYSCONFDIR?=	${PREFIX}/system/etc/dhcpcd

MAN=		dhcpcd.conf.5 dhcpcd.8 dhcpcd-run-hooks.8
CLEANFILES=	dhcpcd.conf.5 dhcpcd.8 dhcpcd-run-hooks.8

SCRIPTS=	dhcpcd-run-hooks
SCRIPTSDIR=	${LIBEXECDIR}
CLEANFILES+=	dhcpcd-run-hooks

FILES=		dhcpcd.conf
FILESDIR=	${SYSCONFDIR}

CPPFLAGS+=	-DDBDIR=\"${DBDIR}\"
CPPFLAGS+=	-DSCRIPT=\"${SCRIPT}\"
CPPFLAGS+=	-DSYSCONFDIR=\"${SYSCONFDIR}\"
LDADD+=		${LIBRT}

SUBDIRS=	dhcpcd-hooks

.SUFFIXES:	.in .sh.in

SED_DBDIR=	-e 's:@DBDIR@:${DBDIR}:g'
SED_HOOKDIR=	-e 's:@HOOKDIR@:${HOOKDIR}:g'
SED_SCRIPT=	-e 's:@SCRIPT@:${SCRIPT}:g'
SED_SYS=	-e 's:@SYSCONFDIR@:${SYSCONFDIR}:g'

.in:
	${SED} ${SED_DBDIR} ${SED_HOOKDIR} ${SED_SCRIPT} ${SED_SYS} $< > $@

.sh.in.sh:
	${SED} ${SED_HOOKDIR} ${SED_SCRIPT} ${SED_SYS} $< > $@

MK=		mk
include ${MK}/sys.mk
include ${MK}/os.mk
include ${MK}/prog.mk

AC_INIT([hildon-home], [0.3.69])
AC_CANONICAL_SYSTEM

AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR([src/hildon-home.c])

AM_MAINTAINER_MODE

AM_INIT_AUTOMAKE

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_LIBTOOL

AC_HEADER_STDC

AC_PATH_X
AC_PATH_XTRA
AC_SUBST(X_CFLAGS)

AM_GCONF_SOURCE_2
AC_PATH_PROG(GCONFTOOL, gconftool-2)

#+++++++++++++++
# Misc programs 
#+++++++++++++++

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(DBUS_BINDING_TOOL, dbus-binding-tool)

#++++++++++++++++++
# Misc build flags
#++++++++++++++++++

AC_ARG_ENABLE(debug, 
	      [AC_HELP_STRING([--enable-debug],[Debug (default=no)])],
              [hildon_use_debug=yes],[hildon_use_debug=no])

AC_ARG_ENABLE(timestamping,
	      [AC_HELP_STRING([--enable-timestamping],[Define HILDON_USE_TIMESTAMPING (default=no)])],
	      [hildon_use_timestamping=yes],[hildon_use_timestamping=no])

AC_ARG_ENABLE(instrumenting,
	      [AC_HELP_STRING([--enable-instrumenting],[Compile with instrumentation flags (default=no)])],
	      [hildon_use_instrumenting=yes],[hildon_use_instrumenting=no])

if test "x${hildon_use_timestamping}" = "xyes"
then
    CFLAGS="$CFLAGS -DHILDON_USE_TIMESTAMPING"
fi

if test "x${hildon_use_instrumenting}" = "xyes"
then
    CFLAGS="$CFLAGS -Wall -Wmissing-prototypes -Wmissing-declarations -Wno-format-extra-args -g -finstrument-functions"
else
    CFLAGS="$CFLAGS -Wall -Wmissing-prototypes -Wmissing-declarations -Wno-format-extra-args -DG_DISABLE_CAST_CHECKS"
fi

AC_SUBST(CFLAGS)

#++++++++++++
# i18n setup 
#++++++++++++

ALL_LINGUAS="en_GB"
AC_SUBST(ALL_LINGUAS)
AM_GLIB_GNU_GETTEXT
AC_DEFINE(GETTEXT_PACKAGE, ["maemo-af-desktop"], [Localisation domain])

PKG_CHECK_MODULES(HILDON_HOME,
                  [hildon-1		>= 2.1.4	dnl
		   hildon-fm-2		>= 2.0.9	dnl
                   libhildondesktop-1	>= 2.1.37	dnl
		   sqlite3				dnl
		   osso-bookmark-engine			dnl
		   mce 					dnl
		   libosso                              dnl
		   x11                                  dnl
		   hildon-thumbnail])

AC_SUBST(HILDON_HOME_LIBS)
AC_SUBST(HILDON_HOME_CFLAGS)

PKG_CHECK_MODULES(HILDON_SV_NOTIFICATION_DAEMON,
                  [glib-2.0 dnl
		   gmodule-2.0 dnl
		   dbus-glib-1])

AC_SUBST(HILDON_SV_NOTIFICATION_DAEMON_LIBS)
AC_SUBST(HILDON_SV_NOTIFICATION_DAEMON_CFLAGS)

# Check for ftw
AC_CHECK_HEADERS([ftw.h],
                 AC_DEFINE([HAVE_FTW_H], 1,
                           [Define to 1 if ftw.h is available]))
AC_CHECK_FUNCS([nftw])

AC_MSG_CHECKING([for GNU ftw extensions])
AC_TRY_COMPILE([#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <ftw.h>], [int flags = FTW_ACTIONRETVAL;], gtk_ok=yes, gtk_ok=no)
if test $gtk_ok = yes; then
  AC_MSG_RESULT([yes])
  AC_DEFINE(HAVE_GNU_FTW, 1, [Have GNU ftw])
else
  AC_MSG_RESULT([no])
fi

#+++++++++++++++++++
# Directories setup
#+++++++++++++++++++

hildondesktopconfdir=${sysconfdir}/hildon-desktop
AC_SUBST(hildondesktopconfdir)

hildonhomedesktopentrydir=`pkg-config libhildondesktop-1 --variable=hildonhomedesktopentrydir`
AC_SUBST(hildonhomedesktopentrydir)
hildonnotificationdesktopentrydir=`pkg-config libhildondesktop-1 --variable=hildonnotificationdesktopentrydir`
AC_SUBST(hildonnotificationdesktopentrydir)

hildondesktoplibdir=`pkg-config libhildondesktop-1 --variable=hildondesktoplibdir`
AC_SUBST(hildondesktoplibdir)

# Maemolauncher
AC_ARG_ENABLE([maemo-launcher],
              [AS_HELP_STRING([--enable-maemo-launcher],
                              [build with maemo-launcher support])],
                              [case "${enableval}" in
                               yes) maemo_launcher=true ;;
                               no)  maemo_launcher=false ;;
                               *) AC_MSG_ERROR([bad value ${enableval} for --enable-maemo-launcher]) ;; esac], [maemo_launcher=false])

if test x$maemo_launcher = xtrue
then
	PKG_CHECK_MODULES(MAEMO_LAUNCHER, [maemo-launcher-app])
	AC_SUBST(MAEMO_LAUNCHER_CFLAGS)
	MAEMO_LAUNCHER_LIBS="-Xcompiler \"$MAEMO_LAUNCHER_LIBS\""
	AC_SUBST(MAEMO_LAUNCHER_LIBS)
fi

AC_OUTPUT([
Makefile
com.nokia.HildonHome.service
org.freedesktop.Notifications.service
com.nokia.HildonSVNotificationDaemon.service
src/Makefile
src/home.conf
src/home.plugins
src/home.safe-set
src/notification.conf
src/notification.safe-set
])

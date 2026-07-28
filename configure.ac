AC_INIT([1983], [0.1.0], [], [1983])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_PROG_CC
AS_IF([test -n "$CC"], [AM_CFLAGS="$AM_CFLAGS -std=c11"])
AC_SUBST([AM_CFLAGS])

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([SDL3], [sdl3], [],
                  [AC_MSG_ERROR([SDL3 development files not found])])
AC_CHECK_LIB([m], [sin], [], [AC_MSG_ERROR([libm not found])])

PROG_GIT_COMMIT=`git -C "$srcdir" rev-parse --short HEAD 2>/dev/null || echo unknown`
AC_MSG_NOTICE([git commit: $PROG_GIT_COMMIT])
AC_SUBST([PROG_GIT_COMMIT])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

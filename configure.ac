AC_INIT([1983], [0.2.0], [https://github.com/salvogendut/1983/issues], [1983],
        [https://github.com/salvogendut/1983])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_PROG_CC
AS_IF([test -n "$CC"], [AM_CFLAGS="$AM_CFLAGS -std=c11"])
AC_SUBST([AM_CFLAGS])

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([SDL3], [sdl3], [],
                  [AC_MSG_ERROR([SDL3 development files not found])])
AC_CHECK_LIB([m], [sin], [], [AC_MSG_ERROR([libm not found])])

AC_PATH_PROG([FFMPEG], [ffmpeg], [])
AS_IF([test -n "$FFMPEG"],
      [AC_DEFINE_UNQUOTED([FFMPEG_PATH], ["$FFMPEG"],
                          [Path to ffmpeg binary])
       AC_DEFINE([HAVE_FFMPEG], [1],
                 [ffmpeg available for GIF optimization])
       AC_MSG_NOTICE([GIF optimization enabled via $FFMPEG])],
      [AC_MSG_NOTICE([GIF optimization disabled (ffmpeg not found)])])

case $host_os in
  mingw*|cygwin*|msys*)
    AC_CHECK_TOOL([WINDRES], [windres], [windres])
    WIN_RC_OBJ="icons/1983-rc.o"
    WIN_STACK_LDFLAGS="-Wl,--stack,16777216"
    NETWORK_LIBS="-lws2_32 -liphlpapi"
    ;;
  *)
    WIN_RC_OBJ=""
    WIN_STACK_LDFLAGS=""
    NETWORK_LIBS=""
    ;;
esac
AC_SUBST([WIN_RC_OBJ])
AC_SUBST([WIN_STACK_LDFLAGS])
AC_SUBST([NETWORK_LIBS])
AC_SUBST([WINDRES])

PROG_GIT_COMMIT=`git -C "$srcdir" rev-parse --short HEAD 2>/dev/null || echo unknown`
AC_MSG_NOTICE([git commit: $PROG_GIT_COMMIT])
AC_SUBST([PROG_GIT_COMMIT])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

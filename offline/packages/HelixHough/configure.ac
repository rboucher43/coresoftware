AC_INIT([HelixHough], [1.00])
AC_CONFIG_SRCDIR([configure.ac])

AM_INIT_AUTOMAKE

AC_PROG_CXX(CC g++)
AC_ENABLE_STATIC(no)

LT_INIT([disable-static])

dnl   no point in suppressing warnings people should 
dnl   at least see them, so here we go for g++: -Wall
if test $ac_cv_prog_gxx = yes; then
    CXXFLAGS="$CXXFLAGS -Wall -Wextra -Werror"
fi

case $CXX in
 clang++)
   CXXFLAGS="$CXXFLAGS -Wno-deprecated-copy"
 ;;
esac

ROOTLIBS=`$ROOTSYS/bin/root-config --libs`
AC_SUBST(ROOTLIBS)

CINTDEFS=" -noIncludePaths  -inlineInputHeader -Wno-inconsistent-missing-override "
AC_SUBST(CINTDEFS)

AC_CONFIG_FILES([
	Makefile
	FitNewton/Makefile
	Seamstress/Makefile
	helix_hough/Makefile
	helix_hough/circlegen/Makefile
	helix_hough/test/Makefile
])
AC_OUTPUT

#!/bin/sh
# Run this to generate all the initial makefiles, etc.

PROJECT=salfet

TEST_TYPE=-f
FILE=autogen.sh

DIE=0

ACLOCAL=aclocal
AUTOHEADER=autoheader
AUTOCONF=autoconf
PKGCONFIG=pkg-config

($ACLOCAL --version) < /dev/null > /dev/null 2>&1 || {
        echo "You must have $ACLOCAL installed to compile $PROJECT."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at http://ftp.gnu.org/gnu/automake/"
        echo
        DIE=1
}

($AUTOHEADER --version) < /dev/null > /dev/null 2>&1 || {
        echo "You must have $AUTOHEADER installed to compile $PROJECT."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at http://ftp.gnu.org/gnu/autoconf/"
        echo
        DIE=1
}

($AUTOCONF --version) < /dev/null > /dev/null 2>&1 || {
        echo "You must have $AUTOCONF installed to compile $PROJECT."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at http://ftp.gnu.org/gnu/autoconf/"
        echo
        DIE=1
}

($PKGCONFIG --version) < /dev/null > /dev/null 2>&1 || {
        echo "You must have $PKGCONFIG installed to compile $PROJECT."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at http://pkg-config.freedesktop.org/"
        echo
        DIE=1
}

if test "$DIE" -eq 1; then
        exit 1
fi

test $TEST_TYPE $FILE || {
        echo "You must run this script in the top-level $PROJECT directory"
        echo
        exit 1
}

if test -z "$*"; then
        echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
        echo
fi

$ACLOCAL || echo "$ACLOCAL failed"
$AUTOHEADER || echo "$AUTOHEADER failed"
$AUTOCONF || echo "$AUTOCONF failed"

run_configure=true
show_help=false
for arg in $*; do
    case $arg in
        --help)
            show_help=true
            ;;
        --no-configure)
            run_configure=false
            ;;
        *)
            ;;
    esac
done

if $run_configure; then
	if $show_help; then
		./configure "$@" | sed 's/\.\/configure/\.\/autogen.sh/g'
	else
		./configure "$@"
		rc=$?
		if [ $rc != 0 ]; then
			exit $rc
		fi
		echo
		echo "Now type 'make' to compile $PROJECT."
	fi
else
    echo
    echo "Now run 'configure' and 'make' to compile $PROJECT."
fi

#!/bin/sh

# This is a generic script for firing up a server, waiting for it to write
# its stringified IOR to a file, then firing up a client

if test "z$MATECORBA_TMPDIR" = "z"; then
	MATECORBA_TMPDIR="/tmp/matecorba-$USER/tst"
	rm -Rf $MATECORBA_TMPDIR
	mkdir -p $MATECORBA_TMPDIR
fi
TMPDIR=$MATECORBA_TMPDIR;
export TMPDIR;

# 100: socket path max - Posix.1g
SAMPLE_NAME="$MATECORBA_TMPDIR/matecorba-$USER/linc-78fe-0-14c0fc671d5b4";
echo "Sample name: '$SAMPLE_NAME'"
if (test ${#SAMPLE_NAME} -gt 100); then
    echo "Socket directory path '$MATECORBA_TMPDIR' too long for bind";
    exit 1;
else
    echo "Running with socketdir: '$MATECORBA_TMPDIR'";
fi

run_test() {
    
    echo testing with $1

    ./server $1 &

    until test -s iorfile; do sleep 1; done
	
	# Causes build problems on Launchpad
    if ./client $1; then
	echo "============================================================="
	echo "Test passed with params: $1"
	echo "============================================================="
	rm iorfile
    else
        echo "============================================================="
	echo "Test failed with params: $1"
	echo "  if this is an IPv4 test, can you ping `hostname` ?"
        echo "============================================================="
	kill $!
	test x"$DONT_EXIT" = x && exit 1
	rm iorfile
    fi
}

for params in '--ORBIIOPIPv4=1 --ORBIIOPUSock=0 --ORBCorbaloc=1' \
              '--ORBIIOPIPv4=1 --ORBIIOPUSock=0 --thread-tests'	 \
	      '--ORBIIOPIPv4=1 --ORBIIOPUSock=0'		 \
	      '--ORBIIOPIPv4=1 --ORBIIOPUSock=0 --thread-safe'	 \
	      '--ORBIIOPIPv4=1 --ORBIIOPUSock=0 --gen-imodule'
do

    run_test "$params"
done

# Don't run the Unix domain socket tests on Windows
if test x"$WINDIR" = x; then
    for params in '--ORBIIOPIPv4=0 --ORBIIOPUSock=1 --ORBCorbaloc=1' \
		  '--ORBIIOPIPv4=0 --ORBIIOPUSock=1 --thread-tests'  \
		  '--ORBIIOPIPv4=0 --ORBIIOPUSock=1'		     \
		  '--ORBIIOPIPv4=0 --ORBIIOPUSock=1 --thread-safe'   \
		  '--ORBIIOPIPv4=0 --ORBIIOPUSock=1 --gen-imodule'
    do

	run_test "$params"
    done
fi

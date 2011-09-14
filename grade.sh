#! /bin/bash

verbose=false

if [ "x$1" = "x-v" ]
then
	verbose=true
	out=/dev/stdout
	err=/dev/stderr
else
	out=/dev/null
	err=/dev/null
fi

if gmake --version >/dev/null 2>&1; then make=gmake; else make=make; fi

pts=5
timeout=30
preservefs=n
readline_hackval=0
qemu=`$make -s --no-print-directory which-qemu`

echo_n () {
	# suns can't echo -n, and Mac OS X can't echo "x\c"
	echo "$@" | tr -d '
'
}

psleep () {
	# solaris "sleep" doesn't take fractions
	perl -e "select(undef, undef, undef, $1);"
}

run () {
	# Find the address of the kernel readline function,
	# which the kernel monitor uses to read commands interactively.
	brkaddr=`grep 'readline$' obj/kernel.sym | sed -e's/ .*$//g'`
	#echo "brkaddr $brkaddr"

	cp /dev/null jos.in
	cp /dev/null jos.out
	echo $qemu -nographic -hda obj/kernel.img -serial null -parallel file:jos.out
	(
		ulimit -t $timeout
		tail -f jos.in | $qemu -nographic -hda obj/kernel.img -serial null -parallel file:jos.out
	) >$out 2>$err &

	psleep 0.1
	grep -q "^Welcome to the JOS kernel monitor" <(tail -f jos.out) >/dev/null
	echo "x" >>jos.in
	rm jos.in
}



$make
run

score=0

	echo_n "Printf: "
	if grep "6828 decimal is 15254 octal!" jos.out >/dev/null
	then
		score=`expr 20 + $score`
		echo OK
	else
		echo WRONG
	fi

	echo_n "Backtrace: "
	cnt=`grep "ebp f01.* eip f01.* args" jos.out | wc -l`
	if [ $cnt -eq 8 ]
	then
		score=`expr 15 + $score`
		echo_n "Count OK"
	else
		echo_n "Count WRONG"
	fi

	cnt=`grep "ebp f01.* eip f01.* args" jos.out | awk 'BEGIN { FS = ORS = " " }
{ print $7 }
END { printf("\n") }' | grep '^00000000 00000000 00000001 00000002 00000003 00000004 00000005' | wc -w`
	if [ $cnt -eq 8 ]; then
		score=`expr 15 + $score`
		echo , Args OK
	else
		echo , Args WRONG
	fi

	echo_n "Debugging symbols: "
	cnt=`grep "kern/init.c.*test_backtrace.*1 arg)" jos.out | wc -l`
	if [ $cnt -eq 6 ]; then
		score=`expr 25 + $score`
		echo OK
	else
		echo WRONG
	fi

echo "Score: $score/75"

if [ $score -lt 75 ]; then
	exit 1
fi



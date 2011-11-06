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
keystrokes=
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

	readline_hack=`grep 'readline_hack$' obj/kernel.sym | sed -e's/ .*$//g' | sed -e's/^f//'`

	cp /dev/null jos.in
	cp /dev/null jos.out
	echo $qemu -nographic -hda obj/kernel.img -serial null -parallel file:jos.out

	ulimit -t $timeout
	(
		grep -q "^Welcome to the JOS kernel monitor" <(tail -f jos.out 2>/dev/null) >/dev/null
		while [ -n "$keystrokes" ]; do
			firstchar=`echo "$keystrokes" | sed -e 's/^\(.\).*/\1/'`
			keystrokes=`echo "$keystrokes" | sed -e 's/^.//'`
			if [ "$firstchar" = ';' ]; then
				echo "sendkey ret"
			elif [ "$firstchar" = ' ' ]; then
				echo "sendkey spc"
			else
				echo "sendkey $firstchar"
			fi
			psleep 0.05
		done
		echo "quit"
	) | $qemu -nographic -hda obj/kernel.img -serial null -parallel file:jos.out -monitor stdio >$out
}



keystrokes="exit;"
$make
run

score=0

echo_n "Physical page allocator: "
 if grep "page_alloc_check() succeeded!" jos.out >/dev/null
 then
	score=`expr 15 + $score`
	echo OK
 else
	echo WRONG
 fi

echo_n "Page management: "
 if grep "page_check() succeeded!" jos.out >/dev/null
 then
	score=`expr 20 + $score`
	echo OK
 else
	echo WRONG
 fi

echo_n "Kernel page directory: "
 if grep "boot_mem_check() succeeded!" jos.out >/dev/null
 then
	score=`expr 15 + $score`
	echo OK
 else
	echo WRONG
 fi

echo_n "Kernel breakpoint interrupt: "
 if grep "^TRAP frame at 0x" jos.out >/dev/null \
     && grep "  trap 0x00000003 Breakpoint" jos.out >/dev/null
 then
	score=`expr 10 + $score`
	echo OK
 else
	echo WRONG
 fi

echo_n "Returning from breakpoint interrupt: "
 if grep "Breakpoint succeeded" jos.out >/dev/null
 then
	score=`expr 10 + $score`
	echo OK
 else
	echo WRONG
 fi

echo "Score: $score/70"



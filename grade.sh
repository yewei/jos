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
	) | (
		$qemu -nographic -hda obj/kernel.img -serial null -parallel file:jos.out -monitor stdio >$out 2>&1
		echo "Welcome to the JOS kernel monitor (NOT)" >>jos.out
	)
}


# Usage: runtest <tagname> <defs> <strings...>
runtest () {
	perl -e "print '$1: '"
	rm -f obj/kern/init.o obj/kernel obj/kernel.img 
	[ "$preservefs" = y ] || rm -f obj/fs.img
	if $verbose
	then
		echo "$make $2... "
	fi
	$make $2 >$out
	if [ $? -ne 0 ]
	then
		echo $make $2 failed 
		exit 1
	fi
	run
	if [ ! -s jos.out ]
	then
		echo 'no jos.out'
	else
		shift
		shift
		continuetest "$@"
	fi
}

quicktest () {
	perl -e "print '$1: '"
	shift
	continuetest "$@"
}

continuetest () {
	okay=yes

	not=false
	for i
	do
		if [ "x$i" = "x!" ]
		then
			not=true
		elif $not
		then
			if egrep "^$i\$" jos.out >/dev/null
			then
				echo "got unexpected line '$i'"
				if $verbose
				then
					exit 1
				fi
				okay=no
			fi
			not=false
		else
			egrep "^$i\$" jos.out >/dev/null
			if [ $? -ne 0 ]
			then
				echo "missing '$i'"
				if $verbose
				then
					exit 1
				fi
				okay=no
			fi
			not=false
		fi
	done
	if [ "$okay" = "yes" ]
	then
		score=`expr $pts + $score`
		echo OK
	else
		echo WRONG
	fi
}

# Usage: runtest1 [-tag <tagname>] <progname> [-Ddef...] STRINGS...
runtest1 () {
	if [ $1 = -tag ]
	then
		shift
		tag=$1
		prog=$2
		shift
		shift
	else
		tag=$1
		prog=$1
		shift
	fi
	runtest1_defs=
	while expr "x$1" : 'x-D.*' >/dev/null; do
		runtest1_defs="DEFS+='$1' $runtest1_defs"
		shift
	done
	runtest "$tag" "DEFS='-DTEST=_binary_obj_user_${prog}_start' DEFS+='-DTESTSIZE=_binary_obj_user_${prog}_size' $runtest1_defs" "$@"
}



score=0

runtest1 hello \
	'.00000000. new env 00001000' \
	'hello, world' \
	'i am environment 00001000' \
	'.00001000. exiting gracefully' \
	'.00001000. free env 00001000' \
	'Idle loop - nothing more to do!'

# the [00001000] tags should have [] in them, but that's 
# a regular expression reserved character, and i'll be damned if
# I can figure out how many \ i need to add to get through 
# however many times the shell interprets this string.  sigh.

runtest1 buggyhello \
	'.00001000. user_mem_check va 00000...' \
	'.00001000. free env 00001000'

runtest1 evilhello \
	'.00001000. user_mem_check va f0100...' \
	'.00001000. free env 00001000'

runtest1 divzero \
	! '1/0 is ........!' \
	'Incoming TRAP frame at 0xefffff..' \
	'  trap 0x00000000 Divide error' \
	'  eip  0x008.....' \
	'  ss   0x----0023' \
	'.00001000. free env 00001000'

runtest1 breakpoint \
	'Welcome to the JOS kernel monitor!' \
	'Incoming TRAP frame at 0xefffffbc' \
	'  trap 0x00000003 Breakpoint' \
	'  eip  0x008.....' \
	'  ss   0x----0023' \
	! '.00001000. free env 00001000'

runtest1 softint \
	'Welcome to the JOS kernel monitor!' \
	'Incoming TRAP frame at 0xefffffbc' \
	'  trap 0x0000000d General Protection' \
	'  eip  0x008.....' \
	'  ss   0x----0023' \
	'.00001000. free env 00001000'

runtest1 badsegment \
	'Incoming TRAP frame at 0xefffffbc' \
	'  trap 0x0000000d General Protection' \
	'  err  0x00000028' \
	'  eip  0x008.....' \
	'  ss   0x----0023' \
	'.00001000. free env 00001000'

runtest1 faultread \
	! 'I read ........ from location 0!' \
	'.00001000. user fault va 00000000 ip 008.....' \
	'Incoming TRAP frame at 0xefffffbc' \
	'  trap 0x0000000e Page Fault' \
	'  err  0x00000004' \
	'.00001000. free env 00001000'

runtest1 faultreadkernel \
	! 'I read ........ from location 0xf0100000!' \
	'.00001000. user fault va f0100000 ip 008.....' \
	'Incoming TRAP frame at 0xefffffbc' \
	'  trap 0x0000000e Page Fault' \
	'  err  0x00000005' \
	'.00001000. free env 00001000' \

runtest1 faultwrite \
	'.00001000. user fault va 00000000 ip 008.....' \
	'Incoming TRAP frame at 0xefffffbc' \
	'  trap 0x0000000e Page Fault' \
	'  err  0x00000006' \
	'.00001000. free env 00001000'

runtest1 faultwritekernel \
	'.00001000. user fault va f0100000 ip 008.....' \
	'Incoming TRAP frame at 0xefffffbc' \
	'  trap 0x0000000e Page Fault' \
	'  err  0x00000007' \
	'.00001000. free env 00001000'

runtest1 testbss \
	'Making sure bss works right...' \
	'Yes, good.  Now doing a wild write off the end...' \
	'.00001000. user fault va 00c..... ip 008.....' \
	'.00001000. free env 00001000'

pts=30
runtest1 dumbfork \
	'.00000000. new env 00001000' \
	'.00001000. new env 00001001' \
	'0: I am the parent!' \
	'9: I am the parent!' \
	'0: I am the child!' \
	'9: I am the child!' \
	'19: I am the child!' \
	'.00001000. exiting gracefully' \
	'.00001000. free env 00001000' \
	'.00001001. exiting gracefully' \
	'.00001001. free env 00001001'

pts=10
keystrokes='backtrace;'
runtest1 -tag 'breakpoint [backtrace]' breakpoint \
	'^Stack backtrace:' \
	' *user/breakpoint.c:.*' \
	' *lib/libmain.c:.*' \
	' *lib/entry.S:.*'

echo Score: $score/100




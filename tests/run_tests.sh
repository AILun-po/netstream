#!/bin/sh

run_test () {
	echo -n "Running test $1 ($2)... "
	../netstream -c ${1}.conf > /dev/null 2>&1
	NSPID=$!
}
check_result () {
	diff ${1}.in ${2}.out > /dev/null
	if [ $? = 0 ]
	then
		echo "OK"
	else
		echo "Failed"
	fi
}

rm -f 1.out
run_test 1 "file -> file"
check_result "a" 1

rm -f 2.out
nc -lp 3000 > 2.out & > /dev/null 2>&1
NCPID=$!
sleep 1
run_test 2 "file -> TCP"
sleep 1
check_result "a" 2
kill $NCPID >/dev/null 2>&1

UDP=0
while [ $UDP -lt 8192 ]
do
	rm -f 3.out
	nc -lup 3000 > 3.out & > /dev/null 2>&1
	NCPID=$!
	run_test 3 "file -> UDP"
	sleep 1
	UDP=`cat 3.out | wc -c`
	echo -n $UDP "-> "
	check_result "b" 3
	kill $NCPID >/dev/null 2>&1
done


rm -f 5.out
nc -lup 3001 > 5.out & >/dev/null 2>&1
NC1PID=$!
sleep 1
run_test 5 "UDP -> UDP" & >/dev/null 2>&1
sleep 1
nc -u 127.0.0.1 3000 < a.in & >/dev/null 2>&1
NC2PID=$!
sleep 1
check_result "a" 5
kill $NC2PID >/dev/null 2>&1
kill $NC1PID >/dev/null 2>&1
kill $NSPID >/dev/null 2>&1

rm -f 4.out
nc -lp 3001 > 4.out & >/dev/null 2>&1
NC1PID=$!
sleep 1
run_test 4 "TCP -> TCP" & >/dev/null 2>&1
sleep 1
nc 127.0.0.1 3000 < a.in & >/dev/null 2>&1
NC2PID=$!
sleep 1
kill $NC2PID >/dev/null 2>&1
sleep 1
check_result "a" 4
kill $NC1PID >/dev/null 2>&1
kill $NSPID >/dev/null 2>&1

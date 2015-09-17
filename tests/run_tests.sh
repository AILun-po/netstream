#!/bin/sh

run_test () {
	if [ x$3 != xq ]
	then
		echo -n "Running test $1 ($2)... "
	fi
	if [ x$3 != xb ]
	then
		../netstream -c ${1}.conf > /dev/null 2>&1
	#	../netstream -c ${1}.conf -v7 
		RES=$?
	else
		../netstream -c ${1}.conf > /dev/null 2>&1 & >/dev/null 2>&1
	#	../netstream -c ${1}.conf -v7 & >/dev/null 2>&1 
		NSPID=$!
	fi
}
check_result () {
	diff ${1}.in ${2}.out > /dev/null
	if [ $? = 0 ]
	then
		RES=0
		return;
	else
		RES=1
		return;
	fi
}
print_result() {
	if [ $RES -eq 0 ]
	then
		if [ x$1 = x ]
		then
			echo "OK"
		fi
	else
		echo "Failed"
		FAIL=1
	fi
}
qkill () {
	kill $1 >/dev/null 2>&1
}

RES=1	# Result of a test
FAIL=0	# Any of tests failed

# Test 1
rm -f 1.out
run_test 1 "file -> file"
print_result q
check_result "a" 1
print_result

#Test 2
rm -f 2.out
nc -lp 3000 > 2.out & > /dev/null 2>&1
NCPID=$!
sleep 1
run_test 2 "file -> TCP"
print_result q
sleep 1
check_result "a" 2
print_result
qkill $NCPID

#Test 3
for i in `seq 10`
do
	rm -f 3.out
	nc -lup 3000 > 3.out & > /dev/null 2>&1
	NCPID=$!
	if [ $i -eq 1 ]
	then
		run_test 3 "file -> UDP"
	else 
		run_test 3 "file -> UDP" q
	fi
	print_result q
	sleep 1
	#UDP=`cat 3.out | wc -c`
	#echo -n $UDP "-> "
	check_result "b" 3
	qkill $NCPID 
	if [ $RES -eq 1 ]
	then
		echo -n "."
	else
		break
	fi
	
done
print_result

# Test 4
rm -f 4.out
nc -lp 3001 > 4.out & >/dev/null 2>&1
NC1PID=$!
sleep 1
run_test 4 "TCP -> TCP" b
sleep 1
nc 127.0.0.1 3000 < a.in & >/dev/null 2>&1
NC2PID=$!
sleep 1
qkill $NC2PID
sleep 1
check_result "a" 4
print_result
qkill $NC1PID
qkill $NSPID

# Test 5
rm -f 5.out
nc -lup 3001 > 5.out & >/dev/null 2>&1
NC1PID=$!
sleep 1
run_test 5 "UDP -> UDP" b
sleep 1
nc -u 127.0.0.1 3000 < a.in & >/dev/null 2>&1
NC2PID=$!
sleep 1
check_result "a" 5
print_result
qkill $NC2PID 
qkill $NC1PID
qkill $NSPID

# Test 6
rm -f 6.*.out
run_test 6 "file -> more files"
print_result q
SUM=0
check_result "a" "6.1"
SUM=$(($SUM+$RES))
check_result "a" "6.2"
SUM=$(($SUM+$RES))
check_result "a" "6.3"
SUM=$(($SUM+$RES))
check_result "a" "6.4"
SUM=$(($SUM+$RES))
check_result "a" "6.5"
SUM=$(($SUM+$RES))
RES=$SUM
print_result

# Test 7
rm -f 7.out
run_test 7 "Retry test" b
nc -l -p 3001 > 7.out & >/dev/null 2>&1
NC1PID=$!
sleep 5
qkill $NC1PID
if [ -s 7.out ]
then 
	echo -n "Retry..."
	rm 7.out
	nc -l -p 3001 > 7.out & >/dev/null 2>&1
	NC1PID=$!
	sleep 3
	qkill $NC1PID
	if [ -s 7.out ]
	then
		echo "OK"
	else
		echo "Failed"
		FAIL=1
	fi

else
	echo "Failed"
	FAIL=1
fi
qkill $NSPID

# Test 8 - invalid config file
run_test 8 "Wrong config test"
if [ $RES -eq 1 ]
then
	echo "OK"
else
	echo "Failed"
	FAIL=1
fi

if [ $FAIL -eq 0 ]
then
	echo "All tests successfully passed"
	exit 0;
else
	echo "Some tests failed"
	exit 1;
fi

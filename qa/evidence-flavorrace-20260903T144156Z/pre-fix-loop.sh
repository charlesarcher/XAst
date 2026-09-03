#!/bin/bash
# PRE-fix same-second flavor-switch race loop (GNU Make 3.81).
# 4 iterations: make BACKEND=VK && make BACKEND=MTL; flavor-check MTL
# 1 iteration:  make BACKEND=VK && make BACKEND=VK; flavor-check VK
# Each make call's full output goes to its own log file.
# Verdict per iteration: BROKEN iff (any make rc != 0) OR (XAsteroids missing)
# OR (flavor-check rc != 0). make rc=0 alone is NOT pass.
set -u
EVID=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(cd "$EVID/../.." && pwd)   # repo root = parent of qa/ (evid dir lives in qa/)
cd "$ROOT" || exit 9

run_pair() { # $1=iter $2=first-backend $3=second-backend $4=expected-check
	local i=$1 b1=$2 b2=$3 exp=$4
	local l1="$EVID/pre-$i-$b1-make.log" l2="$EVID/pre-$i-$b2-make.log" lf="$EVID/pre-$i-flavorcheck-$exp.log"
	make BACKEND=$b1 >"$l1" 2>&1; local r1=$?
	make BACKEND=$b2 >"$l2" 2>&1; local r2=$?
	local exists=MISSING; [ -f "$ROOT/XAsteroids" ] && exists=PRESENT
	qa/flavor-check.sh "$exp" >"$lf" 2>&1; local rf=$?
	local broken=NO
	[ "$r1" -ne 0 ] && broken=YES
	[ "$r2" -ne 0 ] && broken=YES
	[ "$exists" = MISSING ] && broken=YES
	[ "$rf" -ne 0 ] && broken=YES
	echo "iter=$i pair=$b1->$b2 make1_rc=$r1 make2_rc=$r2 binary=$exists flavorcheck_rc=$rf verdict=$broken"
}

: > "$EVID/pre-loop-summary.log"
for i in 1 2 3 4; do run_pair "$i" VK MTL MTL; done | tee "$EVID/pre-loop-summary.log"
run_pair 5 VK VK VK >> "$EVID/pre-loop-summary.log" 2>&1
# summary line for the 5th (tee'd already)
grep -c "verdict=YES" "$EVID/pre-loop-summary.log" | xargs -I{} echo "BROKEN_ITERATIONS_PRE={}/5"
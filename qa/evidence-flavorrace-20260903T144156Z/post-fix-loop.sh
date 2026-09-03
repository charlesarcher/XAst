#!/bin/bash
# POST-fix same-second flavor-switch loop: 15 iterations (14x VK->MTL + 1x VK->VK).
# Same verdict logic as pre: BROKEN iff any make rc != 0 OR XAsteroids missing
# OR flavor-check rc != 0. make rc=0 alone is NOT pass.
set -u
EVID=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(cd "$EVID/../.." && pwd)
cd "$ROOT" || exit 9

run_pair() { # $1=iter $2=first $3=second $4=expected
	local i=$1 b1=$2 b2=$3 exp=$4
	local l1="$EVID/post-$i-$b1-make.log" l2="$EVID/post-$i-$b2-make.log" lf="$EVID/post-$i-flavorcheck-$exp.log"
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

: > "$EVID/post-loop-summary.log"
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do run_pair "$i" VK MTL MTL; done | tee "$EVID/post-loop-summary.log"
run_pair 15 VK VK VK >> "$EVID/post-loop-summary.log" 2>&1
grep -c "verdict=YES" "$EVID/post-loop-summary.log" | xargs -I{} echo "BROKEN_ITERATIONS_POST={}/15"
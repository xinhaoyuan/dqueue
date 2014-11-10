#!/bin/sh

CCFLAGS='-DLOCK=0 -DHELP_THRESHOLD=0' make clean test; mv test test_spin
CCFLAGS='-DLOCK=1 -DHELP_THRESHOLD=0' make clean test; mv test test_0
CCFLAGS='-DLOCK=1 -DHELP_THRESHOLD=1' make clean test; mv test test_1
CCFLAGS='-DLOCK=1 -DHELP_THRESHOLD=2' make clean test; mv test test_2
CCFLAGS='-DLOCK=1 -DHELP_THRESHOLD=5' make clean test; mv test test_5
CCFLAGS='-DLOCK=1 -DHELP_THRESHOLD=100' make clean test; mv test test_100

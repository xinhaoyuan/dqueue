#!/bin/sh

CCFLAGS='-DLOCK=0' make clean test && mv test test_spin
CCFLAGS='-DLOCK=1 -DHELP_THRESHOLD=0' make clean test && mv test test_0
CCFLAGS='-DLOCK=1 -DHELP_THRESHOLD=1' make clean test && mv test test_1

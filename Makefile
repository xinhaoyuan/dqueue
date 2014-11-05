.PHONY: clean

CCFLAGS := -DHELP_THRESHOLD=10
CC := gcc -O2 ${CCFLAGS}
# CC := gcc -O0 -g ${CCFLAGS}
test: test.c dqueue.o
	${CC} $^ -o $@ -pthread

dqueue.o: dqueue.c dqueue.h
	${CC} -c dqueue.c 

clean:
	-rm test dqueue.o

CC=g++
CPPFLAGS=-g -O3 -Wall -std=c++14

all: ccsat

SAT.o: SAT.cc SAT.h
	$(CC) -o $@ -c $< $(CPPFLAGS)

ccsat.o: ccsat.cc SAT.h
	$(CC) -o $@ -c $< $(CPPFLAGS)

ccsat: SAT.o ccsat.o
	$(CC) -o $@ $^ $(CPPFLAGS)

.PHONY: clean
clean:
	rm -f *.o ccsat

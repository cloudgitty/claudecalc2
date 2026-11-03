# nlh9max build. `make` builds the tools into build/, `make test` runs the unit tests.
CC = gcc
CFLAGS = -O3 -march=native -Wall -Isrc/core/include
CORE = src/core/cards.c src/core/handeval.c src/core/streetsolver.c src/core/threadpool.c src/core/icm.c src/core/f16.c
LIBS = -lpthread -lm

all: build/equity build/preflop build/postflop build/test_rake

build:
	mkdir -p build

build/equity: src/tools/equity.c $(CORE) | build
	$(CC) $(CFLAGS) -fopenmp -o $@ src/tools/equity.c src/core/cards.c src/core/handeval.c $(LIBS)

build/preflop: src/preflop.c | build
	$(CC) $(CFLAGS) -fopenmp -o $@ src/preflop.c $(LIBS)

build/postflop: src/tools/postflop.c $(CORE) | build
	$(CC) $(CFLAGS) -o $@ src/tools/postflop.c $(CORE) $(LIBS)

build/test_rake: tests/test_rake.c $(CORE) | build
	$(CC) $(CFLAGS) -o $@ tests/test_rake.c $(CORE) $(LIBS)

test: all
	./build/test_rake
	python3 tests/test_rake.py

.PHONY: all test

# Builds the ICMCore C engine, its tests and the command-line tools on Linux/macOS.
#   make            core library, tests and tools (optimised)
#   make test       fast test suite
#   make test-full  adds the full 133,784,560-hand evaluator enumeration
#   make asan       fast test suite under AddressSanitizer + UndefinedBehaviorSanitizer
#   make clean

CC      ?= cc
BUILD   ?= build
OPT     ?= -O3 -DNDEBUG
CORE    := ICMKit/Sources/ICMCore
CFLAGS  := -std=c11 -Wall -Wextra -Wshadow -D_POSIX_C_SOURCE=200809L -I$(CORE)/include $(OPT) $(EXTRA_CFLAGS)
LDLIBS  := -lpthread -lm

CORE_SRC := $(wildcard $(CORE)/*.c)
CORE_OBJ := $(patsubst $(CORE)/%.c,$(BUILD)/core/%.o,$(CORE_SRC))
HEADERS  := $(wildcard $(CORE)/include/*.h) $(wildcard tests/*.h) $(wildcard tools/*.h)
LIB      := $(BUILD)/libicmcore.a

TESTS     := test_icm test_handeval test_flopclass test_toyriver test_icmsanity test_solver
TOOLS     := buckets bench estimate
TEST_BIN  := $(addprefix $(BUILD)/,$(TESTS))
TOOL_BIN  := $(addprefix $(BUILD)/,$(TOOLS))

all: $(LIB) $(TEST_BIN) $(TOOL_BIN)

$(BUILD)/core/%.o: $(CORE)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(CORE_OBJ)
	ar rcs $@ $^

$(BUILD)/test_%: tests/test_%.c $(LIB) $(HEADERS)
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD)/%: tools/%.c $(LIB) $(HEADERS)
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -lz -o $@

test: $(TEST_BIN)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; $$t; done

test-full: $(BUILD)/test_handeval test
	$(BUILD)/test_handeval --full

asan:
	$(MAKE) BUILD=build-asan OPT="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all" test

clean:
	rm -rf build build-asan

.PHONY: all test test-full asan clean
.SECONDARY:

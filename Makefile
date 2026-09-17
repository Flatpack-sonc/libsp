CC ?= cc
CXX ?= c++
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror
SANITIZE ?=
CPPFLAGS += -Iinclude
LDFLAGS ?=

SRC = src/sp_arena.c src/sp_str.c src/sp_utf8.c src/sp_path.c
OBJ = $(SRC:.c=.o)

ifneq ($(SANITIZE),)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

.PHONY: all test example fuzz clean cpp

all: libsp.a tests/sp_tests examples/join_under

libsp.a: $(OBJ)
	$(AR) rcs $@ $^

src/%.o: src/%.c include/sp.h src/sp_internal.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -fvisibility=hidden -c $< -o $@

tests/sp_tests: tests/test_sp.c libsp.a
	$(CC) $(CFLAGS) $(CPPFLAGS) $< libsp.a $(LDFLAGS) -o $@

examples/join_under: examples/join_under.c libsp.a
	$(CC) $(CFLAGS) $(CPPFLAGS) $< libsp.a $(LDFLAGS) -o $@

fuzz/fuzz_sp: fuzz/fuzz_sp.c libsp.a
	$(CC) $(CFLAGS) $(CPPFLAGS) $< libsp.a $(LDFLAGS) -o $@

tests/sp_cpp: tests/test_cpp.cpp libsp.a
	$(CXX) -std=c++17 -Wall -Wextra -Werror $(CPPFLAGS) tests/test_cpp.cpp libsp.a $(LDFLAGS) -o $@

test: tests/sp_tests
	./tests/sp_tests

example: examples/join_under
	./examples/join_under

cpp: tests/sp_cpp
	./tests/sp_cpp

fuzz: fuzz/fuzz_sp
	./fuzz/fuzz_sp

clean:
	rm -f $(OBJ) libsp.a tests/sp_tests tests/sp_cpp examples/join_under fuzz/fuzz_sp
	rm -rf build

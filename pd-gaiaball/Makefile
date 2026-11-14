# Test target (self-contained). The pd-lib-builder build is added later and
# must coexist with this: test binaries live under build-test/ (gitignored).
TEST_CC ?= cc
TEST_CFLAGS = -std=c99 -Wall -Wextra -Werror -O1
TEST_DIR = build-test

.PHONY: test test-clean
test: $(TEST_DIR)/test_decode
	./$(TEST_DIR)/test_decode

$(TEST_DIR)/test_decode: tests/test_decode.c src/gb_decode.c src/gb_decode.h
	mkdir -p $(TEST_DIR)
	$(TEST_CC) $(TEST_CFLAGS) -Isrc tests/test_decode.c src/gb_decode.c -lm -o $@

test-clean:
	rm -rf $(TEST_DIR)

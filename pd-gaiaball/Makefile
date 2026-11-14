# Test target (self-contained). The pd-lib-builder build is added later and
# must coexist with this: test binaries live under build-test/ (gitignored).
TEST_CC ?= cc
TEST_CFLAGS = -std=c99 -Wall -Wextra -Werror -O1
TEST_DIR = build-test

.PHONY: test test-san test-clean
test: $(TEST_DIR)/test_decode $(TEST_DIR)/test_ws
	./$(TEST_DIR)/test_decode
	./$(TEST_DIR)/test_ws

$(TEST_DIR)/test_decode: tests/test_decode.c src/gb_decode.c src/gb_decode.h
	mkdir -p $(TEST_DIR)
	$(TEST_CC) $(TEST_CFLAGS) -Isrc tests/test_decode.c src/gb_decode.c -lm -o $@

WS_SRC = src/gb_ws.c src/gb_sha1.c src/gb_base64.c
WS_HDR = src/gb_ws.h src/gb_sha1.h src/gb_base64.h

$(TEST_DIR)/test_ws: tests/test_ws.c $(WS_SRC) $(WS_HDR)
	mkdir -p $(TEST_DIR)
	$(TEST_CC) $(TEST_CFLAGS) -Isrc tests/test_ws.c $(WS_SRC) -o $@

# Optional: same tests under ASan/UBSan (not part of `test`).
test-san: tests/test_ws.c $(WS_SRC) $(WS_HDR)
	mkdir -p $(TEST_DIR)
	$(TEST_CC) $(TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -g -Isrc tests/test_ws.c $(WS_SRC) -o $(TEST_DIR)/test_ws_san
	./$(TEST_DIR)/test_ws_san

test-clean:
	rm -rf $(TEST_DIR)

# [gaiaball] for Pure Data.
#
#   make                              build the external with pd-lib-builder (git submodule)
#   make PDINCLUDEDIR=<pd>/src        ... when m_pd.h is not found automatically
#   make test                         C unit tests; needs neither Pd nor its headers
#
# pd-lib-builder requires `all` to be the default goal, so its include comes before the
# test rules.

lib.name = gaiaball

class.sources = gaiaball.c
gaiaball.class.sources = src/gb_decode.c src/gb_ws.c src/gb_sha1.c src/gb_base64.c

cflags = -Isrc

datafiles = gaiaball-help.pd README.md TUTORIAL.md
# The tutorial's example patches, installed as a subfolder so README/TUTORIAL links work.
datadirs = exemplos

define forWindows
  ldlibs += -lws2_32
endef

TEST_GOALS = test test-san test-clean
PDLIBBUILDER_DIR = pd-lib-builder
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter-out $(TEST_GOALS),$(MAKECMDGOALS)),)
  only-tests = yes
endif
endif

ifeq ($(only-tests),yes)
  # test goals only: no pd-lib-builder, so no Pd headers or submodule needed
else ifeq ($(wildcard $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder),)
all:
	@echo "pd-lib-builder not found: run 'git submodule update --init' at the repository root" >&2
	@exit 1
else
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder
endif

# ---- C unit tests (self-contained; binaries under build-test/, gitignored) ----
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

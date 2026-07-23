# Build the host side of the presence second factor:
#   pam_aliro.so            the PAM module (Linux-PAM / macOS OpenPAM)
#   aliro-presence-setup    the pairing-key + cred-id helper
# and run the unit tests. This is an install/build makefile, separate from CI
# (CI runs ./run.sh via the top-level `make test-presence`).
#
#   make            # build the module + tool
#   make test       # build + run the unit tests
#   make install    # install the module + tool + a sample config (needs root)

ROOT      := $(abspath ../..)
ALIRO     := $(ROOT)/modules/woz_aliro
OUT       := $(ROOT)/build/presence
CC        ?= cc
CFLAGS    ?= -std=c11 -O2 -Wall -Wextra
INCS      := -I. -I$(ALIRO)/include -I$(ALIRO)/src
DEPS      := aliro_presence.c $(ALIRO)/src/aliro_assert.c $(ALIRO)/src/aliro_hash.c

UNAME_S   := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  SO_EXT  := so
  SHARED  := -bundle -undefined dynamic_lookup
else
  SO_EXT  := so
  SHARED  := -shared
endif

# Install locations (override on the command line).
LIBDIR    ?= /lib/security
BINDIR    ?= /usr/local/sbin
CONFDIR   ?= /etc/aliro-presence

.PHONY: all test install clean
all: $(OUT)/pam_aliro.$(SO_EXT) $(OUT)/aliro-presence-setup

$(OUT):
	@mkdir -p $(OUT)

$(OUT)/pam_aliro.$(SO_EXT): pam_aliro.c $(DEPS) | $(OUT)
	$(CC) $(CFLAGS) -fPIC $(SHARED) $(INCS) $^ -o $@

$(OUT)/aliro-presence-setup: aliro-presence-setup.c aliro_presence.c $(ALIRO)/src/aliro_assert.c $(ALIRO)/src/aliro_hash.c | $(OUT)
	$(CC) $(CFLAGS) $(INCS) $^ -o $@

test:
	@./run.sh

install: all
	install -d $(LIBDIR) $(BINDIR) $(CONFDIR)
	install -m 0644 $(OUT)/pam_aliro.$(SO_EXT) $(LIBDIR)/pam_aliro.$(SO_EXT)
	install -m 0755 $(OUT)/aliro-presence-setup $(BINDIR)/aliro-presence-setup
	install -m 0644 config.sample $(CONFDIR)/config.sample
	@echo "next: sudo aliro-presence-setup keygen  (then edit $(CONFDIR)/config)"

clean:
	rm -rf $(OUT)

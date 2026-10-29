CC ?= gcc
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

all: build

build:
	cargo build --workspace
	mkdir -p target/release
	cp target/debug/super-v-daemon target/release/super-v-daemon
	cp target/debug/super-v-ui target/release/super-v-ui
	strip target/release/super-v-daemon
	strip target/release/super-v-ui

install: build
	mkdir -p $(BINDIR)
	install -m 755 target/release/super-v-daemon $(BINDIR)/super-v-daemon
	install -m 755 target/release/super-v-ui $(BINDIR)/super-v-ui
	mkdir -p $(HOME)/.config/systemd/user
	install -m 644 data/super-v-clipboard.service $(HOME)/.config/systemd/user/super-v-clipboard.service
	mkdir -p $(HOME)/.local/share/applications
	install -m 644 data/super-v-clipboard.desktop $(HOME)/.local/share/applications/super-v-clipboard.desktop
	@echo "Installation complete into $(BINDIR)"

uninstall:
	rm -f $(BINDIR)/super-v-daemon
	rm -f $(BINDIR)/super-v-ui
	rm -f $(HOME)/.config/systemd/user/super-v-clipboard.service
	rm -f $(HOME)/.local/share/applications/super-v-clipboard.desktop
	@echo "Uninstallation complete"

.PHONY: all build install uninstall

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
	mkdir -p $(HOME)/.local/share/gnome-shell/extensions/super-v-bridge@arkhimuttaqina.my.id
	cp -r data/gnome-extension/* $(HOME)/.local/share/gnome-shell/extensions/super-v-bridge@arkhimuttaqina.my.id/
	mkdir -p $(BINDIR)
	install -m 755 target/release/super-v-daemon $(BINDIR)/super-v-daemon
	install -m 755 target/release/super-v-ui $(BINDIR)/super-v-ui
	mkdir -p $(HOME)/.config/systemd/user
	install -m 644 data/super-v-clipboard.service $(HOME)/.config/systemd/user/super-v-clipboard.service
	mkdir -p $(HOME)/.local/share/applications
	install -m 644 data/super-v-clipboard.desktop $(HOME)/.local/share/applications/super-v-clipboard.desktop
	@echo "Updating desktop database..."
	@update-desktop-database $(HOME)/.local/share/applications 2>/dev/null || true
	@echo "Enabling & starting systemd service..."
	@systemctl --user daemon-reload 2>/dev/null || true
	@systemctl --user enable --now super-v-clipboard.service 2>/dev/null || true
	@echo "Auto-applying GNOME Super+V shortcut & running diagnostics..."
	@$(BINDIR)/super-v-ui --check
	@echo "=========================================================="
	@echo " Installation & Auto-Configuration Complete!"
	@echo " Press Super+V or launch 'Super-V Clipboard' from Drawer."
	@echo "=========================================================="

uninstall:
	systemctl --user stop super-v-clipboard.service 2>/dev/null || true
	systemctl --user disable super-v-clipboard.service 2>/dev/null || true
	rm -f $(BINDIR)/super-v-daemon
	rm -f $(BINDIR)/super-v-ui
	rm -f $(HOME)/.config/systemd/user/super-v-clipboard.service
	rm -f $(HOME)/.local/share/applications/super-v-clipboard.desktop
	update-desktop-database $(HOME)/.local/share/applications 2>/dev/null || true
	@echo "Uninstallation complete"

check:
	$(BINDIR)/super-v-ui --check

.PHONY: all build install uninstall check

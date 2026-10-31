CC ?= gcc
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
EXT_UUID := super-v-bridge@arkhimuttaqina.my.id

all: build

build:
	cargo build --release --workspace
	mkdir -p target/out
	cp target/release/super-v-daemon target/out/super-v-daemon
	cp target/release/super-v-ui target/out/super-v-ui
	cp target/release/super-v-toggle target/out/super-v-toggle
	cp target/release/super-v-ingest target/out/super-v-ingest
	cp target/release/super-v-x11watch target/out/super-v-x11watch
	strip target/out/super-v-daemon target/out/super-v-ui target/out/super-v-toggle target/out/super-v-ingest target/out/super-v-x11watch

install: build
	mkdir -p $(HOME)/.local/share/gnome-shell/extensions/$(EXT_UUID)
	cp -r data/gnome-extension/* $(HOME)/.local/share/gnome-shell/extensions/$(EXT_UUID)/
	mkdir -p $(BINDIR)
	install -m 755 target/out/super-v-daemon $(BINDIR)/super-v-daemon
	install -m 755 target/out/super-v-ui $(BINDIR)/super-v-ui
	install -m 755 target/out/super-v-toggle $(BINDIR)/super-v-toggle
	install -m 755 target/out/super-v-x11watch $(BINDIR)/super-v-x11watch
	install -m 755 target/out/super-v-ingest $(BINDIR)/super-v-ingest
	mkdir -p $(HOME)/.config/systemd/user
	install -m 644 data/super-v-clipboard.service $(HOME)/.config/systemd/user/super-v-clipboard.service
	install -m 644 data/super-v-x11watch.service $(HOME)/.config/systemd/user/super-v-x11watch.service
	mkdir -p $(HOME)/.local/share/applications
	install -m 644 data/super-v-clipboard.desktop $(HOME)/.local/share/applications/super-v-clipboard.desktop
	@update-desktop-database $(HOME)/.local/share/applications 2>/dev/null || true
	@systemctl --user daemon-reload 2>/dev/null || true
	@systemctl --user enable --now super-v-clipboard.service 2>/dev/null || true
	@systemctl --user enable --now super-v-x11watch.service 2>/dev/null || true
	@$(BINDIR)/super-v-ui --check
	@echo "=========================================================="
	@echo " Installation & Auto-Configuration Complete!"
	@echo " Press Super+V or launch 'Super-V Clipboard' from Drawer."
	@echo "=========================================================="

uninstall:
	systemctl --user stop super-v-clipboard.service 2>/dev/null || true
	systemctl --user disable super-v-clipboard.service 2>/dev/null || true
	rm -f $(BINDIR)/super-v-daemon $(BINDIR)/super-v-ui $(BINDIR)/super-v-toggle $(BINDIR)/super-v-ingest $(BINDIR)/super-v-x11watch
	rm -f $(HOME)/.config/systemd/user/super-v-clipboard.service
	rm -f $(HOME)/.config/systemd/user/super-v-x11watch.service
	rm -f $(HOME)/.local/share/applications/super-v-clipboard.desktop
	update-desktop-database $(HOME)/.local/share/applications 2>/dev/null || true

clean:
	cargo clean

.PHONY: all build install uninstall clean

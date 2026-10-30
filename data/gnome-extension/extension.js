// SPDX-FileCopyrightText: 2026 Arkhi Muttaqina
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Super-V bridge: everything a Wayland client cannot do for itself.
//  - move the overlay next to the pointer (move_frame + activate)
//  - paste into the focused app with a virtual keyboard (Ctrl+V, or
//    Ctrl+Shift+V when a terminal owns the focus)
//  - watch clipboard changes while the overlay is not focused and hand the
//    text to super-v-ingest (not a Wayland client, so the dock never flashes)
// The keystroke recipes and terminal detection follow Clipman, which is known
// to work in terminals and editors.

import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const BUS_NAME = 'my.id.arkhimuttaqina.Bridge';
const OBJECT_PATH = '/my/id/arkhimuttaqina/Bridge';
const IFACE_XML = `
<node>
  <interface name="my.id.arkhimuttaqina.Bridge">
    <method name="MoveWindowToCursor">
      <arg type="s" name="title" direction="in"/>
    </method>
    <method name="SimulatePaste">
      <arg type="s" name="mode" direction="in"/>
    </method>
    <method name="InsertText">
      <arg type="s" name="text" direction="in"/>
    </method>
    <method name="Ping">
      <arg type="s" name="reply" direction="out"/>
    </method>
  </interface>
</node>`;

const APP_HINTS = ['superv', 'super-v'];

const TEXT_TYPES = [
    'text/plain;charset=utf-8',
    'UTF8_STRING',
    'text/plain',
    'STRING',
    'text/uri-list',
];
const MAX_BYTES = 1024 * 1024;

// Terminals paste with Ctrl+Shift+V. Matched against the whole lower-cased
// app id or its last dotted segment, never as a substring.
const TERMINAL_APP_IDS = new Set([
    'org.gnome.terminal', 'gnome-terminal-server', 'gnome-terminal',
    'org.gnome.ptyxis', 'org.gnome.ptyxis.devel', 'ptyxis',
    'org.gnome.console', 'org.gnome.console.devel', 'kgx',
    'com.gexperts.tilix', 'tilix', 'kitty', 'alacritty', 'terminator',
    'xterm', 'uxterm', 'org.kde.konsole', 'konsole', 'foot', 'footclient',
    'org.wezfurlong.wezterm', 'wezterm', 'st', 'st-256color', 'sakura',
    'xfce4-terminal', 'mate-terminal', 'lxterminal', 'guake', 'tilda',
    'cool-retro-term', 'com.raggesilver.blackbox', 'com.mitchellh.ghostty',
    'io.elementary.terminal', 'terminology', 'rio',
]);

function _isTerminal(appId) {
    const id = (appId ?? '').toLowerCase();
    if (!id)
        return false;
    return TERMINAL_APP_IDS.has(id) ||
        TERMINAL_APP_IDS.has(id.slice(id.lastIndexOf('.') + 1));
}

const PASTE_RECIPES = Object.assign(Object.create(null), {
    'ctrl-v': {modifiers: ['Control_L'], key: 'v'},
    'ctrl-shift-v': {modifiers: ['Control_L', 'Shift_L'], key: 'v'},
    'shift-insert': {modifiers: ['Shift_L'], key: 'Insert'},
});

const KEY_LOOKUP = Object.assign(Object.create(null), {
    'Control_L': Clutter.KEY_Control_L,
    'Shift_L': Clutter.KEY_Shift_L,
    'v': Clutter.KEY_v,
    'Insert': Clutter.KEY_Insert,
});

export default class SuperVBridgeExtension extends Extension {
    enable() {
        this._dbus = Gio.DBusExportedObject.wrapJSObject(IFACE_XML, this);
        this._dbus.export(Gio.DBus.session, OBJECT_PATH);
        this._nameId = Gio.bus_own_name(
            Gio.BusType.SESSION, BUS_NAME, Gio.BusNameOwnerFlags.NONE,
            null, null, null);

        this._selection = global.display.get_selection();
        this._ownerChangedId = this._selection.connect(
            'owner-changed', this._onOwnerChanged.bind(this));
        this._timeout = 0;
        this._virtualKeyboard = null;
        this._prevFocus = null;
        this._source = null;
        this._lastText = '';
        this._pollId = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT, 800, () => {
                this._pollSelection();
                return GLib.SOURCE_CONTINUE;
            });
    }

    _pollSelection() {
        if (!this._source)
            return;
        try {
            this._readSelection(this._source);
        } catch {
            // The source died; the next owner-changed replaces it.
        }
    }

    disable() {
        if (this._pollId) {
            GLib.source_remove(this._pollId);
            this._pollId = 0;
        }
        this._source = null;
        if (this._timeout) {
            GLib.source_remove(this._timeout);
            this._timeout = 0;
        }
        if (this._ownerChangedId) {
            this._selection.disconnect(this._ownerChangedId);
            this._ownerChangedId = 0;
        }
        this._selection = null;
        this._prevFocus = null;
        if (this._virtualKeyboard) {
            this._virtualKeyboard.run_dispose();
            this._virtualKeyboard = null;
        }
        if (this._nameId) {
            Gio.bus_unown_name(this._nameId);
            this._nameId = 0;
        }
        if (this._dbus) {
            this._dbus.unexport();
            this._dbus = null;
        }
    }

    // ---- typed paste ----------------------------------------------------

    _backend() {
        // Clutter.get_default_backend() was removed in GNOME Shell 51.
        return Clutter.get_default_backend
            ? Clutter.get_default_backend()
            : global.stage.context.get_backend();
    }

    _getVirtualKeyboard() {
        if (!this._virtualKeyboard) {
            const seat = this._backend().get_default_seat();
            this._virtualKeyboard = seat.create_virtual_device(
                Clutter.InputDeviceType.KEYBOARD_DEVICE);
        }
        return this._virtualKeyboard;
    }

    _resolveRecipe(mode) {
        if (typeof mode === 'string' && Object.hasOwn(PASTE_RECIPES, mode))
            return PASTE_RECIPES[mode];

        const focus = global.display.get_focus_window();
        const appId = focus ? (focus.get_gtk_application_id?.() ||
            focus.get_wm_class() || '') : '';
        return _isTerminal(appId)
            ? PASTE_RECIPES['ctrl-shift-v'] : PASTE_RECIPES['ctrl-v'];
    }

    _dispatchKeystroke(recipe) {
        const vk = this._getVirtualKeyboard();
        if (!vk)
            throw new Error('virtual keyboard unavailable');

        const pressed = [];
        try {
            for (const mod of recipe.modifiers) {
                vk.notify_keyval(Clutter.CURRENT_TIME,
                    KEY_LOOKUP[mod], Clutter.KeyState.PRESSED);
                pressed.push(mod);
            }
            vk.notify_keyval(Clutter.CURRENT_TIME,
                KEY_LOOKUP[recipe.key], Clutter.KeyState.PRESSED);
            vk.notify_keyval(Clutter.CURRENT_TIME,
                KEY_LOOKUP[recipe.key], Clutter.KeyState.RELEASED);
        } finally {
            for (const mod of pressed) {
                vk.notify_keyval(Clutter.CURRENT_TIME,
                    KEY_LOOKUP[mod], Clutter.KeyState.RELEASED);
            }
        }
    }

    _restoreFocusThen(callback) {
        const prev = this._prevFocus;
        this._prevFocus = null;
        let delay = 0;
        if (prev && !this._matches(prev, 'Super-V Clipboard')) {
            try {
                prev.activate(global.get_current_time());
                delay = 80;
            } catch {
                delay = 0;
            }
        }
        if (delay === 0) {
            callback();
            return;
        }
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
            callback();
            return GLib.SOURCE_REMOVE;
        });
    }

    SimulatePasteAsync([mode], invocation) {
        try {
            const recipe = this._resolveRecipe(mode);
            this._restoreFocusThen(() => {
                try {
                    this._dispatchKeystroke(recipe);
                } catch {
                    // Nothing to recover; the next pick retries.
                }
            });
            invocation.return_value(null);
        } catch (e) {
            invocation.return_dbus_error(
                'my.id.arkhimuttaqina.Bridge.Error', e.message);
        }
    }

    InsertTextAsync([text], invocation) {
        try {
            this._restoreFocusThen(() => {
                try {
                    this._insertText(text);
                } catch {
                    // Nothing to recover; the caller falls back to Ctrl+V.
                }
            });
            invocation.return_value(null);
        } catch (e) {
            invocation.return_dbus_error(
                'my.id.arkhimuttaqina.Bridge.Error', e.message);
        }
    }

    _insertText(text) {
        if (!text)
            return;
        const vk = this._getVirtualKeyboard();
        if (!vk)
            throw new Error('virtual keyboard unavailable');
        const now = Clutter.CURRENT_TIME;
        for (const ch of text) {
            const cp = ch.codePointAt(0);
            let keysym;
            if (ch === '\n')
                keysym = 0xff0d;
            else if (ch === '\t')
                keysym = 0xff09;
            else if (cp <= 0xffff)
                keysym = cp;
            else
                keysym = 0x01000000 + cp;
            vk.notify_keyval(now, keysym, Clutter.KeyState.PRESSED);
            vk.notify_keyval(now, keysym, Clutter.KeyState.RELEASED);
        }
    }

    // ---- clipboard capture ----------------------------------------------

    _onOwnerChanged(_selection, selectionType, source) {
        if (selectionType !== Meta.SelectionType.SELECTION_CLIPBOARD)
            return;
        this._source = source;
        if (this._timeout)
            GLib.source_remove(this._timeout);
        this._timeout = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 120, () => {
            this._timeout = 0;
            this._readSelection(source);
            return GLib.SOURCE_REMOVE;
        });
    }

    _readSelection(source) {
        if (!source)
            return;
        const offered = source.get_mimetypes();
        const mime = TEXT_TYPES.find(t => offered.includes(t));
        if (!mime)
            return;

        let stream;
        try {
            stream = source.transfer_async(mime, Gio.IO_PRIORITY_DEFAULT, null);
        } catch {
            return;
        }

        stream.read_bytes_async(MAX_BYTES, GLib.PRIORITY_DEFAULT, null,
            (src, res) => {
                let bytes;
                try {
                    bytes = src.read_bytes_finish(res);
                } catch {
                    return;
                }
                if (!bytes || bytes.get_size() === 0)
                    return;
                let text;
                try {
                    text = new TextDecoder('utf-8').decode(bytes.get_data());
                } catch {
                    return;
                }
                if (text === this._lastText)
                    return;
                this._lastText = text;
                this._handOff(text);
            });
    }

    _handOff(text) {
        if (!text || !text.trim())
            return;
        const path = GLib.build_filenamev([
            GLib.get_tmp_dir(),
            `super-v-ingest-${GLib.get_monotonic_time()}.txt`,
        ]);
        try {
            GLib.file_set_contents(path, text);
        } catch {
            return;
        }
        const exe = GLib.find_program_in_path('super-v-ingest') ||
            GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', 'super-v-ingest']);
        try {
            GLib.spawn_async(null, [exe, path], null, GLib.SpawnFlags.NONE, null);
        } catch {
            try {
                GLib.unlink(path);
            } catch {
                // The next copy retries.
            }
        }
    }

    // ---- placement ------------------------------------------------------

    Ping() {
        return 'pong';
    }

    MoveWindowToCursorAsync([title], invocation) {
        try {
            this._moveWindowToCursor(title);
            invocation.return_value(null);
        } catch (e) {
            invocation.return_dbus_error(
                'my.id.arkhimuttaqina.Bridge.Error', e.message);
        }
    }

    _matches(metaWin, title) {
        if (!metaWin)
            return false;
        const cls = (metaWin.get_wm_class() || '').toLowerCase();
        const appId = (metaWin.get_gtk_application_id?.() || '').toLowerCase();
        const winTitle = metaWin.get_title() || '';
        if (title && winTitle === title)
            return true;
        return APP_HINTS.some(h => cls.includes(h) || appId.includes(h));
    }

    _moveWindowToCursor(title) {
        const [x, y] = global.get_pointer();
        const monitor = global.display.get_current_monitor();
        const workArea = global.display.get_workspace_manager()
            .get_active_workspace().get_work_area_for_monitor(monitor);
        const gap = 18;
        const edge = 8;

        for (const actor of global.get_window_actors()) {
            const metaWin = actor.get_meta_window();
            if (!this._matches(metaWin, title))
                continue;

            const rect = metaWin.get_frame_rect();
            let winX = x + gap;
            let winY = y + gap;
            if (winX + rect.width > workArea.x + workArea.width)
                winX = x - rect.width - gap;
            if (winY + rect.height > workArea.y + workArea.height)
                winY = y - rect.height - gap;
            winX = Math.max(workArea.x + edge, Math.min(winX,
                workArea.x + workArea.width - rect.width - edge));
            winY = Math.max(workArea.y + edge, Math.min(winY,
                workArea.y + workArea.height - rect.height - edge));

            const focused = global.display.get_focus_window();
            if (focused && focused !== metaWin) {
                this._prevFocus = focused;
            }
            metaWin.move_frame(true, winX, winY);
            metaWin.activate(global.get_current_time());
            return;
        }
        throw new Error(`no Super-V window matching "${title}"`);
    }
}

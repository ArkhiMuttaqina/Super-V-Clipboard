// SPDX-FileCopyrightText: 2026 Arkhi Muttaqina
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Super-V bridge. Two jobs that a Wayland client cannot do for itself:
//  1. move the overlay next to the pointer (move_frame + activate),
//  2. notice clipboard changes while the overlay is not focused, and hand the
//     text to super-v-ingest (a non-Wayland helper, so the dock never flashes).
// The placement technique mirrors Clipman's MoveWindowToCursor.

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
    }

    disable() {
        if (this._timeout) {
            GLib.source_remove(this._timeout);
            this._timeout = 0;
        }
        if (this._ownerChangedId) {
            this._selection.disconnect(this._ownerChangedId);
            this._ownerChangedId = 0;
        }
        this._selection = null;
        if (this._nameId) {
            Gio.bus_unown_name(this._nameId);
            this._nameId = 0;
        }
        if (this._dbus) {
            this._dbus.unexport();
            this._dbus = null;
        }
    }

    // ---- clipboard capture -------------------------------------------

    _onOwnerChanged(_selection, selectionType, source) {
        if (selectionType !== Meta.SelectionType.SELECTION_CLIPBOARD)
            return;
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
                // Nothing else to do; the next copy retries.
            }
        }
    }

    // ---- placement ----------------------------------------------------

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

            metaWin.move_frame(true, winX, winY);
            metaWin.activate(global.get_current_time());
            return;
        }
        throw new Error(`no Super-V window matching "${title}"`);
    }
}

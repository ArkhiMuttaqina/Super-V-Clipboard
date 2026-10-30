// SPDX-FileCopyrightText: 2026 Super-V Clipboard Authors
// SPDX-License-Identifier: GPL-3.0-or-later

/* Super-V Clipboard GNOME Shell Bridge
 *
 * Exposes a minimal DBus interface at my.id.arkhimuttaqina.SuperVBridge.
 * Listens to MetaSelection::owner-changed and emits ClipboardChanged signal.
 * Zero UI, zero Clutter actors, zero crash risk for the compositor.
 */

import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const DBUS_IFACE = `
<node>
  <interface name="my.id.arkhimuttaqina.SuperVBridge">
    <signal name="ClipboardChanged">
      <arg type="s" name="mime"/>
    </signal>
    <method name="TriggerPaste"/>
    <method name="GetPointerPosition">
      <arg type="i" direction="out" name="x"/>
      <arg type="i" direction="out" name="y"/>
    </method>
  </interface>
</node>`;

export default class SuperVBridgeExtension extends Extension {
    enable() {
        this._selection = Meta.Selection.new(global.display);
        this._ownerChangedId = this._selection.connect('owner-changed', (sel, type) => {
            if (type === Meta.SelectionType.SELECTION_CLIPBOARD) {
                this._emitClipboardChanged('text/plain');
            }
        });

        this._dbusImpl = Gio.DBusExportedObject.wrapJSObject(DBUS_IFACE, this);
        this._dbusImpl.export(Gio.DBus.session, '/org/arkhimuttaqina/SuperVBridge');
    }

    disable() {
        if (this._ownerChangedId) {
            this._selection.disconnect(this._ownerChangedId);
            this._ownerChangedId = 0;
        }
        if (this._dbusImpl) {
            this._dbusImpl.unexport();
            this._dbusImpl = null;
        }
        this._selection = null;
    }

    _emitClipboardChanged(mime) {
        if (this._dbusImpl) {
            this._dbusImpl.emit_signal('ClipboardChanged', GLib.Variant.new('(s)', [mime]));
        }
    }

    TriggerPaste() {
        // Clutter synthetic keypress dispatch if requested
        return true;
    }

    GetPointerPosition() {
        const [x, y] = global.get_pointer();
        return [x, y];
    }
}

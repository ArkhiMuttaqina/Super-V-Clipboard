// SPDX-FileCopyrightText: 2026 Super-V Clipboard Authors
// SPDX-License-Identifier: GPL-3.0-or-later

import Meta from 'gi://Meta';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const DAEMON_BUS_NAME = 'my.id.arkhimuttaqina.Daemon';
const DAEMON_OBJECT_PATH = '/my/id/arkhimuttaqina/Daemon';

const TEXT_MIME_TYPES = [
    'text/plain;charset=utf-8',
    'UTF8_STRING',
    'text/plain',
    'STRING',
    'text/uri-list',
];

export default class SuperVBridgeExtension extends Extension {
    enable() {
        this._selection = global.display.get_selection();
        this._ownerChangedId = this._selection.connect(
            'owner-changed',
            this._onOwnerChanged.bind(this)
        );
    }

    disable() {
        if (this._clipboardTimeout) {
            GLib.source_remove(this._clipboardTimeout);
            this._clipboardTimeout = null;
        }
        if (this._readCancellable) {
            this._readCancellable.cancel();
            this._readCancellable = null;
        }
        if (this._ownerChangedId) {
            this._selection.disconnect(this._ownerChangedId);
            this._ownerChangedId = null;
        }
        this._selection = null;
    }

    _onOwnerChanged(_selection, selectionType, selectionSource) {
        if (selectionType !== Meta.SelectionType.SELECTION_CLIPBOARD)
            return;

        if (this._clipboardTimeout) {
            GLib.source_remove(this._clipboardTimeout);
            this._clipboardTimeout = null;
        }

        this._clipboardTimeout = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT, 100, () => {
                this._clipboardTimeout = null;
                this._readClipboard(selectionSource);
                return GLib.SOURCE_REMOVE;
            }
        );
    }

    _readClipboard(source) {
        if (!source)
            return;
        const offered = source.get_mimetypes();
        const textTypes = TEXT_MIME_TYPES.filter(t => offered.includes(t));

        if (textTypes.length > 0) {
            this._readText(source, textTypes[0]);
        } else if (offered.some(t => t.startsWith('image/'))) {
            this._sendToDaemon('image/png', '[Image Screenshot]');
        }
    }

    _readText(source, mimeType) {
        this._readCancellable = new Gio.Cancellable();
        this._selection.transfer_async(
            Meta.SelectionType.SELECTION_CLIPBOARD,
            mimeType,
            -1,
            this._readCancellable,
            (selection, result) => {
                try {
                    const stream = selection.transfer_finish(result);
                    if (!stream) return;
                    const dinput = new Gio.DataInputStream({base_stream: stream});
                    const [out, len] = dinput.read_upto('', 0, null);
                    if (out && len > 0) {
                        this._sendToDaemon(mimeType, out);
                    }
                } catch (e) {
                    // Ignore transfer aborts
                }
            }
        );
    }

    _sendToDaemon(mime, content) {
        Gio.DBus.session.call(
            DAEMON_BUS_NAME,
            DAEMON_OBJECT_PATH,
            DAEMON_BUS_NAME,
            'NewEntry',
            new GLib.Variant('(ss)', [mime, content]),
            null,
            Gio.DBusCallFlags.NO_AUTO_START,
            -1,
            null,
            () => {}
        );
    }
}

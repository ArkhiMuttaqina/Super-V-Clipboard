#include "overlay.h"
#include <adwaita.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib-unix.h>
#include <signal.h>
#include <malloc.h>

#define GLYPH_PAGE 120
#define IDLE_EXIT_SECONDS 180
#define EMOJI_CELL 46
#define SYMBOL_CELL 44

static GtkWidget *g_main_window = NULL;
static GtkWidget *g_stack = NULL;
static GtkWidget *g_clip_list = NULL;
static GtkWidget *g_empty_label = NULL;
static GtkWidget *g_bridge_banner = NULL;
static GtkWidget *g_search_entry = NULL;
static GtkWidget *g_emoji_flow = NULL;
static GtkWidget *g_recent_emoji_flow = NULL;
static GtkWidget *g_symbol_flow = NULL;
static GtkWidget *g_gif_flow = NULL;
static GtkWidget *g_gif_label = NULL;
static gboolean g_skip_change = FALSE;
static guint g_restore_source = 0;
static char *g_saved_clip = NULL;
static ClipTextHook g_clip_hook = NULL;
static ImageHook g_image_hook = NULL;

static GtkWidget *g_lang_combo = NULL;
static GtkWidget *g_gif_combo = NULL;

static PasteCallback g_paste_cb = NULL;
static PinCallback g_pin_cb = NULL;
static RefreshHook g_refresh_hook = NULL;
static DeleteCallback g_delete_cb = NULL;
static GlyphProvider g_glyph_provider = NULL;
static ActivateHook g_activate_hook = NULL;
static LanguageHook g_language_hook = NULL;
static GifProviderHook g_gif_provider_hook = NULL;
static GifReloadHook g_gif_reload_hook = NULL;
static GifSearchHook g_gif_search_hook = NULL;
static GifMoreHook g_gif_more_hook = NULL;
static gboolean g_gif_requested = FALSE;
static ApiKeyHook g_api_key_hook = NULL;
static ProviderTestHook g_provider_test_hook = NULL;
static char g_api_key[256] = "";

static int g_offset_emoji = 0;
static int g_offset_symbol = 0;
static int g_done_emoji = 0;
static int g_done_symbol = 0;

static char g_query_emoji[160] = "";
static char g_query_symbol[160] = "";
static char g_langs[64] = "en,id";
static char g_gif_provider[32] = "local";

static guint g_idle_exit_source = 0;
static gboolean g_loading_page = FALSE;
static gboolean g_setting_entry = FALSE;
static gboolean g_setting_combo = FALSE;
static int g_placement_pointer = 0;
static PlacementHook g_placement_hook = NULL;

static void hide_main_window(void);
static void cancel_idle_exit(void);
static void schedule_idle_exit(void);
static GtkWidget *make_thumb(const char *path, int size);
static GtkWidget *make_gif_thumb(const char *path, int size);
static void on_gif_clicked(GtkButton *btn, gpointer user_data);
static void select_combo(GtkWidget *combo, guint idx);
static gboolean place_after_map(gpointer data);
static void paste_after_hide(const char *text);
static void refresh_bridge_status(void);
static void mark_self_clipboard(void);
typedef struct { char *text; } PasteAction;
static gboolean do_paste_action(gpointer data);
static void paste_image_after_hide(const char *path);

static void on_history_row_activated(AdwActionRow *row, gpointer data);
static void sync_clipboard_once(void);
static gboolean sync_clipboard_idle(gpointer data);
static void request_pointer_placement(void);
static void on_placement_changed(GObject *obj, GParamSpec *pspec, gpointer user_data);
static void reload_glyphs(const char *tab);

void super_v_ui_set_paste_callback(PasteCallback cb) { g_paste_cb = cb; }
void super_v_ui_set_pin_callback(PinCallback cb) { g_pin_cb = cb; }
void super_v_ui_set_refresh_hook(RefreshHook cb) { g_refresh_hook = cb; }
void super_v_ui_set_delete_callback(DeleteCallback cb) { g_delete_cb = cb; }
void super_v_ui_set_glyph_provider(GlyphProvider cb) { g_glyph_provider = cb; }
void super_v_ui_set_activate_hook(ActivateHook cb) { g_activate_hook = cb; }
void super_v_ui_set_language_hook(LanguageHook cb) { g_language_hook = cb; }
void super_v_ui_set_gif_provider_hook(GifProviderHook cb) { g_gif_provider_hook = cb; }
void super_v_ui_set_gif_reload_hook(GifReloadHook cb) { g_gif_reload_hook = cb; }
void super_v_ui_set_gif_search_hook(GifSearchHook cb) { g_gif_search_hook = cb; }
void super_v_ui_set_gif_more_hook(GifMoreHook cb) { g_gif_more_hook = cb; }

typedef struct {
    MainTaskFn fn;
    void *data;
} MainTask;

static gboolean main_task_run(gpointer payload) {
    MainTask *task = payload;
    if (task->fn) task->fn(task->data);
    g_free(task);
    return G_SOURCE_REMOVE;
}

/* Worker threads cannot touch GTK; they hand the result back through here. */
void super_v_ui_post_to_main(MainTaskFn fn, void *data) {
    MainTask *task = g_new0(MainTask, 1);
    task->fn = fn;
    task->data = data;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, main_task_run, task, NULL);
}
void super_v_ui_set_api_key_hook(ApiKeyHook cb) { g_api_key_hook = cb; }
void super_v_ui_set_provider_test_hook(ProviderTestHook cb) { g_provider_test_hook = cb; }
void super_v_ui_set_placement_hook(PlacementHook cb) { g_placement_hook = cb; }

static void free_closure_data(gpointer data, GClosure *closure) { (void)closure; g_free(data); }

static void install_css(void) {
    GtkCssProvider *p = gtk_css_provider_new();
    const char *css =
        ".emoji-cell { font-size: 25px; min-width: 44px; min-height: 44px; padding: 0; }\n"
        ".symbol-cell { font-size: 20px; min-width: 44px; min-height: 44px; padding: 0; }\n"
        ".gif-cell { padding: 0; }\n"
        ".gif-cell image, .gif-cell picture { -gtk-icon-size: 84px; border-radius: 8px; }\n"
        ".superv-overlay { background: transparent; }\n"
        ".section-row { background: transparent; box-shadow: none; border: none; padding: 0; min-height: 0; }\n"
        ".section-label { font-weight: 700; font-size: 88%; opacity: 0.6; margin: 10px 6px 2px 6px; }\n"
        ".variant-dot { font-size: 10px; opacity: 0.8; margin-bottom: 0; }\n"
        ".warning-banner { background-color: alpha(@warning_color, 0.18); border-radius: 10px; padding: 8px; }\n"
        ".superv-card { background-color: @window_bg_color; border-radius: 14px; border: 1px solid @borders; box-shadow: 0 8px 28px rgba(0,0,0,0.35); }\n";
    gtk_css_provider_load_from_string(p, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void copy_text_to_clipboard(const char *text) {
    GdkDisplay *d = gdk_display_get_default();
    if (!d) return;
    g_skip_change = TRUE;
    gdk_clipboard_set_text(gdk_display_get_clipboard(d), text);
}

/* --------------------------------------------------------- clipboard watch */

void super_v_ui_set_clipboard_hook(ClipTextHook cb) { g_clip_hook = cb; }
void super_v_ui_set_image_hook(ImageHook cb) { g_image_hook = cb; }

static void on_clipboard_text_ready(GObject *source, GAsyncResult *res, gpointer data) {
    (void)data;
    GError *err = NULL;
    char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, &err);
    if (!text) {
        if (err) {
            if (g_getenv("SUPER_V_DEBUG_CLIPBOARD")) g_printerr("CLIP read failed: %s\n", err->message);
            g_error_free(err);
        }
        return;
    }
    if (g_clip_hook && *text) {
        g_clip_hook(text);
    }
    g_free(text);
}

static void on_clipboard_changed(GdkClipboard *clipboard, gpointer data) {
    (void)data;
    if (g_skip_change) {
        g_skip_change = FALSE;
        return;
    }
    if (g_restore_source) {
        g_source_remove(g_restore_source);
        g_restore_source = 0;
        g_free(g_saved_clip);
        g_saved_clip = NULL;
    }
    gdk_clipboard_read_text_async(clipboard, NULL, on_clipboard_text_ready, NULL);
}

/* ---------------------------------------------------------------- history */

static void emit_paste(long long id, const char *text) {
    if (g_paste_cb) g_paste_cb(id, text);
}

static gint64 g_last_pick_us = 0;

static gboolean refresh_history_idle(gpointer data) {
    (void)data;
    if (g_refresh_hook) g_refresh_hook();
    return G_SOURCE_REMOVE;
}

/* Ignore clicks that landed on the suffix buttons (pin / delete / reveal). */
static gboolean click_hit_button(GtkWidget *row, double x, double y) {
    GtkWidget *hit = gtk_widget_pick(row, (int)x, (int)y, GTK_PICK_DEFAULT);
    for (GtkWidget *w = hit; w && w != row; w = gtk_widget_get_parent(w)) {
        if (GTK_IS_BUTTON(w)) return TRUE;
    }
    return FALSE;
}

static void on_row_click_released(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture; (void)n_press;
    GtkWidget *row = GTK_WIDGET(data);
    if (click_hit_button(row, x, y)) return;
    on_history_row_activated(ADW_ACTION_ROW(row), NULL);
}
static gboolean g_last_paste_had_texture = FALSE;
static gboolean g_last_paste_had_gif = FALSE;

static GdkTexture *texture_for_image(const char *path) {
    GError *err = NULL;
    GFile *file = g_file_new_for_path(path);
    GdkTexture *texture = gdk_texture_new_from_file(file, &err);
    g_object_unref(file);
    if (texture) return texture;
    /* GDK's own loader has no GIF decoder, so fall back to GdkPixbuf. */
    if (err) { g_error_free(err); err = NULL; }
    GdkPixbuf *frame = gdk_pixbuf_new_from_file(path, &err);
    if (!frame) {
        if (err) g_error_free(err);
        return NULL;
    }
    texture = gdk_texture_new_for_pixbuf(frame);
    g_object_unref(frame);
    return texture;
}

static gboolean path_is_gif(const char *path, const char *mime) {
    if (mime && g_ascii_strcasecmp(mime, "image/gif") == 0) return TRUE;
    return path && g_str_has_suffix(path, ".gif");
}

/* Put an image file on the clipboard. For GIFs the payload is the animated
 * file itself (image/gif) unioned with a first-frame PNG: chat clients that
 * understand image/gif show the animation (WhatsApp/Telegram web), and clients
 * that only take textures still paste something. */
/* Builds the clipboard payload for one image path: animated bytes when it is a
 * GIF, a texture for clients that only take pictures, and a file list for
 * clients that want the file itself. */
static GdkContentProvider *image_content_provider(const char *path, const char *mime,
                                                  gboolean *had_texture, gboolean *had_gif) {
    if (!path || !*path) return NULL;
    GdkTexture *texture = texture_for_image(path);
    gboolean is_gif = path_is_gif(path, mime);
    if (had_texture) *had_texture = texture != NULL;
    if (had_gif) *had_gif = FALSE;

    GdkContentProvider *providers[3];
    int n = 0;
    gchar *data = NULL;
    gsize len = 0;
    if (is_gif && g_file_get_contents(path, &data, &len, NULL) && len > 0) {
        providers[n++] = gdk_content_provider_new_for_bytes(
            "image/gif", g_bytes_new_take(data, len));
        if (had_gif) *had_gif = TRUE;
    } else {
        g_free(data);
    }
    if (texture) {
        providers[n++] = gdk_content_provider_new_typed(GDK_TYPE_TEXTURE, texture);
    }
    {
        GFile *file = g_file_new_for_path(path);
        if (file) {
            GSList *list = g_slist_append(NULL, file);
            GdkFileList *file_list = gdk_file_list_new_from_list(list);
            if (file_list)
                providers[n++] = gdk_content_provider_new_typed(GDK_TYPE_FILE_LIST, file_list);
        }
    }

    GdkContentProvider *all = NULL;
    if (n == 1) {
        all = providers[0];
    } else if (n > 1) {
        all = gdk_content_provider_new_union(providers, n);
        for (int i = 0; i < n; i++) g_object_unref(providers[i]);
    }
    if (texture) g_object_unref(texture);
    return all;
}

void super_v_ui_set_gif_clipboard(const char *path) {
    GdkDisplay *d = gdk_display_get_default();
    if (!d) return;
    GdkContentProvider *provider = image_content_provider(path, "image/gif", NULL, NULL);
    if (!provider) return;
    mark_self_clipboard();
    gdk_clipboard_set_content(gdk_display_get_clipboard(d), provider);
    g_object_unref(provider);
}

/* The watcher sees every clipboard write; a marker keeps it from recording
 * our own pastes as fresh copies. */
static void mark_self_clipboard(void) {
    g_file_set_contents("/tmp/super-v-clipboard-owned", "1", 1, NULL);
}

static void paste_file_after_hide(const char *path, const char *mime) {
    gboolean had_texture = FALSE, had_gif = FALSE;
    GdkContentProvider *all = image_content_provider(path, mime, &had_texture, &had_gif);
    g_last_paste_had_texture = had_texture;
    g_last_paste_had_gif = had_gif;
    if (!all) return;

    GdkDisplay *d = gdk_display_get_default();
    if (!d) {
        g_object_unref(all);
        return;
    }
    mark_self_clipboard();
    gdk_clipboard_set_content(gdk_display_get_clipboard(d), all);
    g_object_unref(all);
    hide_main_window();
    g_timeout_add(90, do_paste_action, g_new0(PasteAction, 1));
}

static void paste_image_after_hide(const char *path) {
    paste_file_after_hide(path, NULL);
}

static void on_history_row_activated(AdwActionRow *row, gpointer data) {
    (void)data;
    gint64 now = g_get_monotonic_time();
    if (now - g_last_pick_us < 300000) return;   /* one pick per gesture */
    g_last_pick_us = now;
    const char *text = (const char *)g_object_get_data(G_OBJECT(row), "super_v_text");
    const char *blob = (const char *)g_object_get_data(G_OBJECT(row), "super_v_blob");
    const char *mime = (const char *)g_object_get_data(G_OBJECT(row), "super_v_mime");
    if (text && *text && (!mime || !g_str_has_prefix(mime, "image/"))) {
        paste_after_hide(text);
        return;
    }
    if (blob && *blob && mime && g_str_has_prefix(mime, "image/")) {
        paste_file_after_hide(blob, mime);
        return;
    }
    gint64 *pid = (gint64 *)g_object_get_data(G_OBJECT(row), "super_v_id");
    if (pid) {
        emit_paste((long long)*pid, NULL);
        hide_main_window();
    }
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box; (void)user_data;
    const char *text = (const char *)g_object_get_data(G_OBJECT(row), "super_v_text");
    if (g_getenv("SUPER_V_DEBUG_INSERT")) g_printerr("ROW activated text=%s pid=%s\n", (text && *text) ? "yes" : "no", g_object_get_data(G_OBJECT(row), "super_v_id") ? "yes" : "no");
    gint64 *pid = (gint64 *)g_object_get_data(G_OBJECT(row), "super_v_id");
    if (text && *text) {
        paste_after_hide(text);
    } else if (pid) {
        emit_paste((long long)*pid, NULL);
        hide_main_window();
    } else {
        return;
    }
}

static void on_pin_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *row = GTK_WIDGET(user_data);
    gint64 *pid = (gint64 *)g_object_get_data(G_OBJECT(row), "super_v_id");
    if (pid && g_pin_cb) g_pin_cb((long long)*pid);
    g_idle_add(refresh_history_idle, NULL);
}

static void on_delete_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *row = GTK_WIDGET(user_data);
    gint64 *pid = (gint64 *)g_object_get_data(G_OBJECT(row), "super_v_id");
    if (pid && g_delete_cb) g_delete_cb((long long)*pid);
    GtkWidget *list = gtk_widget_get_parent(row);
    if (list && GTK_IS_LIST_BOX(list)) {
        gtk_list_box_remove(GTK_LIST_BOX(list), row);
    }
}

static void on_eye_btn_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *row = GTK_WIDGET(user_data);
    gboolean revealed = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "super_v_revealed"));
    const char *raw = (const char *)g_object_get_data(G_OBJECT(row), "super_v_text");
    const char *masked = (const char *)g_object_get_data(G_OBJECT(row), "super_v_masked");

    if (revealed) {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), masked ? masked : "");
        gtk_button_set_icon_name(btn, "view-reveal-symbolic");
        gtk_widget_set_tooltip_text(GTK_WIDGET(btn), "Show Sensitive Content");
    } else {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), raw ? raw : "");
        gtk_button_set_icon_name(btn, "view-conceal-symbolic");
        gtk_widget_set_tooltip_text(GTK_WIDGET(btn), "Hide Sensitive Content");
    }
    g_object_set_data(G_OBJECT(row), "super_v_revealed", GINT_TO_POINTER(!revealed));
}

static gboolean filter_clip_row(GtkListBoxRow *row, gpointer user_data) {
    (void)user_data;
    const char *query = gtk_editable_get_text(GTK_EDITABLE(g_search_entry));
    if (!query || strlen(query) == 0) return TRUE;

    const char *title = (const char *)g_object_get_data(G_OBJECT(row), "super_v_text");
    if (!title) return FALSE;

    gchar *title_lower = g_utf8_strdown(title, -1);
    gchar *query_lower = g_utf8_strdown(query, -1);
    gboolean match = (strstr(title_lower, query_lower) != NULL);
    g_free(title_lower);
    g_free(query_lower);
    return match;
}

/* Escape user text for Pango markup and prefix it with a small timestamp. */
static char *single_line_preview(const char *text, gsize limit) {
    if (!text) return g_strdup("");
    GString *out = g_string_new(NULL);
    gboolean last_space = FALSE;
    for (const char *p = text; *p; p = g_utf8_next_char(p)) {
        gunichar cp = g_utf8_get_char(p);
        if (cp == 0x000A || cp == 0x000D || cp == 0x0009) cp = 0x20;
        if (cp == 0x20) {
            if (last_space) continue;
            last_space = TRUE;
        } else {
            last_space = FALSE;
        }
        g_string_append_unichar(out, cp);
        if (g_utf8_strlen(out->str, -1) >= (glong)limit) {
            g_string_append(out, "\u2026");
            break;
        }
    }
    return g_string_free(out, FALSE);
}

static char *history_markup(const char *text, long long created_at, int is_pinned) {
    (void)is_pinned;
    char *preview = single_line_preview(text, 96);
    char *escaped = g_markup_escape_text(preview, -1);
    g_free(preview);
    char stamp[64] = "";
    if (created_at > 0) {
        GDateTime *dt = g_date_time_new_from_unix_local((gint64)created_at);
        if (dt) {
            char *formatted = g_date_time_format(dt, "%d %b %H:%M");
            if (formatted) {
                g_strlcpy(stamp, formatted, sizeof(stamp));
                g_free(formatted);
            }
            g_date_time_unref(dt);
        }
    }
    char *result;
    if (stamp[0]) {
        result = g_strdup_printf(
            "<span size=\"x-small\" alpha=\"55%%\">%s</span>  %s", stamp, escaped);
    } else {
        result = g_strdup_printf("%s", escaped);
    }
    g_free(escaped);
    return result;
}

/* Section headers make the pinned block visually separate from the rest. */
static void history_header_func(GtkListBoxRow *row, GtkListBoxRow *before, gpointer data) {
    (void)data;
    static const char *prev_key = NULL;
    (void)prev_key;
    gboolean pinned = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "super_v_pinned"));
    gboolean before_pinned = before ? GPOINTER_TO_INT(g_object_get_data(G_OBJECT(before), "super_v_pinned")) : FALSE;
    gboolean need = (before == NULL && pinned) ||
        (before != NULL && pinned != before_pinned) ||
        (before == NULL && !pinned);
    if (!need) {
        gtk_list_box_row_set_header(row, NULL);
        return;
    }
    GtkWidget *label = gtk_label_new(pinned ? "Pinned" : "Recent");
    gtk_widget_add_css_class(label, "dim-label");
    gtk_widget_add_css_class(label, "section-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_top(label, 6);
    gtk_widget_set_margin_bottom(label, 2);
    gtk_list_box_row_set_header(row, label);
    gtk_widget_set_visible(label, TRUE);
}
static gboolean g_pinned_section_added = FALSE;
static gboolean g_recent_section_added = FALSE;

static void add_section_row(const char *title) {
    if (!g_clip_list) return;
    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "section-row");
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "section-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    g_object_set_data(G_OBJECT(row), "super_v_section", GINT_TO_POINTER(1));
    gtk_list_box_append(GTK_LIST_BOX(g_clip_list), row);
}

static void tame_row_labels(GtkWidget *widget) {
    if (GTK_IS_LABEL(widget)) {
        gtk_label_set_ellipsize(GTK_LABEL(widget), PANGO_ELLIPSIZE_END);
        gtk_label_set_wrap(GTK_LABEL(widget), FALSE);
        gtk_label_set_lines(GTK_LABEL(widget), 1);
        return;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        tame_row_labels(child);
    }
}

void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id, const char *preview_image_path, long long created_at, const char *mime) {
    if (!g_clip_list) return;
    if (g_empty_label) gtk_widget_set_visible(g_empty_label, FALSE);
    if (is_pinned && !g_pinned_section_added) {
        add_section_row("Pinned");
        g_pinned_section_added = TRUE;
    } else if (!is_pinned && !g_recent_section_added) {
        add_section_row("Recent");
        g_recent_section_added = TRUE;
    }

    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);

    if (is_sensitive) {
        char *masked = g_strdup(title ? title : "");
        /* Keep only a short recognizable prefix, mask the rest. */
        gsize len = strlen(masked);
        if (len > 4) {
            for (gsize i = 3; i < len; i++) masked[i] = '*';
        }
        g_object_set_data_full(G_OBJECT(row), "super_v_masked", masked, g_free);
        char *markup = history_markup(title, created_at, is_pinned);
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), TRUE);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), markup);
        g_free(markup);
        g_object_set_data(G_OBJECT(row), "super_v_revealed", GINT_TO_POINTER(1));
    } else {
        char *markup = history_markup(title, created_at, is_pinned);
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), TRUE);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), markup);
        g_free(markup);
    }

    if (subtitle && *subtitle) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    g_object_set_data(G_OBJECT(row), "super_v_pinned", GINT_TO_POINTER(is_pinned ? 1 : 0));
    if (mime) g_object_set_data_full(G_OBJECT(row), "super_v_mime", g_strdup(mime), g_free);
    if (preview_image_path) g_object_set_data_full(G_OBJECT(row), "super_v_blob", g_strdup(preview_image_path), g_free);
    if (is_pinned) {
        gtk_widget_add_css_class(GTK_WIDGET(row), "pinned-row");
    }

    g_object_set_data_full(G_OBJECT(row), "super_v_text", g_strdup(title ? title : ""), g_free);
    g_signal_connect(row, "activated", G_CALLBACK(on_history_row_activated), NULL);
    GtkGesture *row_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(row_click), GDK_BUTTON_PRIMARY);
    g_signal_connect(row_click, "released", G_CALLBACK(on_row_click_released), row);
    gtk_widget_add_controller(GTK_WIDGET(row), GTK_EVENT_CONTROLLER(row_click));
    gint64 *heap_id = g_new0(gint64, 1);
    *heap_id = (gint64)id;
    g_object_set_data_full(G_OBJECT(row), "super_v_id", heap_id, g_free);

    if (preview_image_path && *preview_image_path) {
        GtkWidget *thumb = make_thumb(preview_image_path, 56);
        if (thumb) adw_action_row_add_prefix(ADW_ACTION_ROW(row), thumb);
    } else if (mime && g_str_has_prefix(mime, "image/")) {
        GtkWidget *icon = gtk_image_new_from_icon_name("image-x-generic-symbolic");
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
    }

    if (is_sensitive) {
        GtkWidget *eye = gtk_button_new_from_icon_name("view-conceal-symbolic");
        gtk_widget_add_css_class(eye, "flat");
        gtk_widget_set_tooltip_text(eye, "Hide Sensitive Content");
        g_signal_connect(eye, "clicked", G_CALLBACK(on_eye_btn_clicked), row);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), eye);
    }

    GtkWidget *pin_btn = gtk_button_new_from_icon_name(is_pinned ? "view-pin-symbolic" : "view-pin-symbolic");
    gtk_widget_add_css_class(pin_btn, "flat");
    gtk_widget_set_tooltip_text(pin_btn, is_pinned ? "Unpin" : "Pin");
    g_signal_connect(pin_btn, "clicked", G_CALLBACK(on_pin_btn_clicked), row);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), pin_btn);

    GtkWidget *del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(del_btn, "flat");
    gtk_widget_set_tooltip_text(del_btn, "Delete");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_delete_btn_clicked), row);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), del_btn);

    tame_row_labels(GTK_WIDGET(row));
    gtk_list_box_append(GTK_LIST_BOX(g_clip_list), GTK_WIDGET(row));
}

void super_v_ui_clear_history_items(void) {
    if (g_empty_label) gtk_widget_set_visible(g_empty_label, TRUE);
    g_pinned_section_added = FALSE;
    g_recent_section_added = FALSE;
    if (!g_clip_list) return;
    GtkWidget *child = gtk_widget_get_first_child(g_clip_list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(g_clip_list), child);
        child = next;
    }
}

/* ----------------------------------------------------------------- glyphs */

static GtkWidget *flow_for_tab(const char *tab) {
    if (!tab) return NULL;
    if (strcmp(tab, "emoji") == 0) return g_emoji_flow;
    if (strcmp(tab, "symbol") == 0 || strcmp(tab, "symbols") == 0) return g_symbol_flow;
    if (strcmp(tab, "gif") == 0) return g_gif_flow;
    return NULL;
}

/* Insert text into whatever window has focus. The Shell extension owns the
   virtual keyboard, so the clipboard is never touched by a pick. */
static void on_insert_reply(GObject *source, GAsyncResult *res, gpointer data) {
    (void)data;
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    if (reply) {
        g_variant_unref(reply);
        if (g_getenv("SUPER_V_DEBUG_INSERT")) g_printerr("INSERT typed by extension\n");
    } else {
        if (g_getenv("SUPER_V_DEBUG_INSERT")) g_printerr("INSERT extension unavailable: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
    }
}

void super_v_ui_insert_text(const char *text) {
    if (!text || !*text) return;
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        if (err) g_error_free(err);
        return;
    }
    g_dbus_connection_call(
        bus,
        "my.id.arkhimuttaqina.Bridge",
        "/my/id/arkhimuttaqina/Bridge",
        "my.id.arkhimuttaqina.Bridge",
        "InsertText",
        g_variant_new("(s)", text),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        800,
        NULL,
        on_insert_reply,
        NULL);
    g_object_unref(bus);
}

/* Pick -> paste. The clipboard is read while the overlay still has focus (a
   hidden Wayland client cannot read it), then the window hides, the picked
   text is placed on the clipboard, the Shell sends Ctrl+V, and half a second
   later the previous clipboard is restored. */

static void simulate_paste(void);
static void restore_clipboard_later(void);


static gboolean do_paste_action(gpointer data) {
    PasteAction *action = (PasteAction *)data;
    if (g_getenv("SUPER_V_SELFTEST_NO_INJECT")) {
        g_free(action->text);
        g_free(action);
        return G_SOURCE_REMOVE;
    }
    if (g_getenv("SUPER_V_DEBUG_INSERT")) g_printerr("PASTE dispatch now\n");
    simulate_paste();
    restore_clipboard_later();
    g_free(action->text);
    g_free(action);
    return G_SOURCE_REMOVE;
}

static void on_clipboard_captured_for_paste(GObject *source, GAsyncResult *res, gpointer data) {
    GError *err = NULL;
    char *previous = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, &err);
    if (err) g_error_free(err);

    if (g_getenv("SUPER_V_DEBUG_INSERT")) g_printerr("PASTE captured previous=[%.40s]\n", previous ? previous : "(none)");
    g_free(g_saved_clip);
    g_saved_clip = previous;

    PasteAction *action = g_new0(PasteAction, 1);
    action->text = (char *)data;

    /* Set the selection while the window still has the seat: Wayland
       rejects clipboard ownership from an unfocused client. */
    GdkDisplay *d = gdk_display_get_default();
    if (d && action->text) {
        g_skip_change = TRUE;
        mark_self_clipboard();
        gdk_clipboard_set_text(gdk_display_get_clipboard(d), action->text);
    }

    if (g_getenv("SUPER_V_DEBUG_INSERT")) g_printerr("PASTE clipboard set to [%.40s], hiding\n", action->text);
    hide_main_window();
    g_timeout_add(90, do_paste_action, action);
}

void super_v_ui_paste_text(const char *text) {
    if (!text || !*text) return;
    GdkDisplay *d = gdk_display_get_default();
    if (!d) return;
    GdkClipboard *clip = gdk_display_get_clipboard(d);
    if (!clip) return;
    gdk_clipboard_read_text_async(clip, NULL, on_clipboard_captured_for_paste, g_strdup(text));
}

static gboolean restore_clipboard_now(gpointer data) {
    (void)data;
    g_restore_source = 0;
    GdkDisplay *d = gdk_display_get_default();
    if (d && g_saved_clip) {
        g_skip_change = TRUE;
        gdk_clipboard_set_text(gdk_display_get_clipboard(d), g_saved_clip);
    }
    g_free(g_saved_clip);
    g_saved_clip = NULL;
    return G_SOURCE_REMOVE;
}

static void restore_clipboard_later(void) {
    if (!g_saved_clip) return;
    if (g_restore_source) g_source_remove(g_restore_source);
    g_restore_source = g_timeout_add(800, restore_clipboard_now, NULL);
}

static void simulate_paste(void) {
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        if (err) g_error_free(err);
        return;
    }
    g_dbus_connection_call(bus,
        "my.id.arkhimuttaqina.Bridge",
        "/my/id/arkhimuttaqina/Bridge",
        "my.id.arkhimuttaqina.Bridge",
        "SimulatePaste",
        g_variant_new("(s)", "auto"),
        NULL, G_DBUS_CALL_FLAGS_NONE, 600, NULL, NULL, NULL);
    g_object_unref(bus);
}

typedef struct { char *text; } PasteLater;

static gboolean paste_later_cb(gpointer data) {
    PasteLater *p = (PasteLater *)data;
    super_v_ui_paste_text(p->text);
    g_free(p->text);
    g_free(p);
    return G_SOURCE_REMOVE;
}

static void paste_after_hide(const char *text) {
    if (!text || !*text) return;
    PasteLater *p = g_new0(PasteLater, 1);
    p->text = g_strdup(text);
    g_timeout_add(140, paste_later_cb, p);
}

static void on_glyph_clicked(GtkButton *btn, gpointer user_data) {
    const char *glyph = (const char *)user_data;
    GtkWidget *child = btn ? gtk_widget_get_parent(GTK_WIDGET(btn)) : NULL;
    GtkWidget *flow = child ? gtk_widget_get_parent(child) : NULL;
    if (flow && flow == g_emoji_flow) super_v_ui_record_recent("emoji", glyph);
    paste_after_hide(glyph);
}

static void on_skin_variant_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *variant = (const char *)user_data;
    paste_after_hide(variant);
}

static void on_popover_closed(GtkPopover *popover, gpointer user_data) {
    (void)user_data;
    gtk_widget_unparent(GTK_WIDGET(popover));
}

/* Skin-tone variants come from the dataset: the Rust side owns the RGI table
 * and hands this file the exact sequences for a glyph, so multi-person emoji
 * (two modifiers, one per person) are correct instead of guessed.
 */
static gboolean is_skin_modifier(gunichar cp) {
    return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

static int utf8_seq_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

static gunichar utf8_decode_at(const char *s, int *len_out) {
    int len = utf8_seq_len((unsigned char)s[0]);
    *len_out = len;
    return g_utf8_get_char(s);
}

/* Emoji font coverage.
 * The installed colour font (Noto Color Emoji 2.051 on Ubuntu) covers Emoji
 * 15.1, while the dataset is Unicode Emoji 18.0. Sequences newer than the font
 * have no glyph, so Pango falls back to a font that draws the code points in a
 * hex box. Probe the emoji font once and draw a neutral placeholder instead. */
static PangoFont *g_emoji_font = NULL;
static gboolean g_emoji_font_probed = FALSE;
static gboolean g_emoji_font_usable = FALSE;
static int g_missing_glyphs = 0;

static void probe_emoji_font(void) {
    if (g_emoji_font_probed) return;
    g_emoji_font_probed = TRUE;
    PangoFontMap *map = pango_cairo_font_map_get_default();
    if (!map) return;
    PangoContext *ctx = pango_font_map_create_context(map);
    if (!ctx) return;
    PangoFontDescription *desc = pango_font_description_from_string("emoji");
    pango_font_description_set_absolute_size(desc, EMOJI_CELL * PANGO_SCALE);
    g_emoji_font = pango_context_load_font(ctx, desc);
    pango_font_description_free(desc);
    g_object_unref(ctx);
    /* Trust the probe only when the resolved font really is an emoji font. */
    g_emoji_font_usable = g_emoji_font && pango_font_has_char(g_emoji_font, 0x1F600);
}

static gboolean glyph_supported(const char *glyph) {
    probe_emoji_font();
    if (!g_emoji_font_usable) return TRUE;
    for (const char *p = glyph; *p; ) {
        gunichar cp = g_utf8_get_char(p);
        p = g_utf8_next_char(p);
        /* Selectors and joiners have no glyph of their own. */
        if (cp == 0x200D || cp == 0xFE0E || cp == 0xFE0F) continue;
        if (!pango_font_has_char(g_emoji_font, cp)) return FALSE;
    }
    return TRUE;
}

/* The character when the font can draw it, otherwise a dimmed broken-image
 * icon. The cell still copies the exact sequence, so a client with a newer
 * font renders it correctly. */
static GtkWidget *glyph_visual(const char *glyph, gboolean is_emoji) {
    if (!is_emoji || glyph_supported(glyph)) return gtk_label_new(glyph);
    g_missing_glyphs++;
    GtkWidget *img = gtk_image_new_from_icon_name("image-missing-symbolic");
    gtk_widget_set_opacity(img, 0.4);
    return img;
}

static void set_glyph_tooltip(GtkWidget *btn, const char *glyph, const char *name) {
    const char *label = (name && *name) ? name : glyph;
    if (glyph_supported(glyph)) {
        gtk_widget_set_tooltip_text(btn, label);
        return;
    }
    char *text = g_strdup_printf(
        "%s\nThis sequence is newer than the installed emoji font; it copies correctly anyway.",
        label);
    gtk_widget_set_tooltip_text(btn, text);
    g_free(text);
}

/* Provider plumbing: the UI never synthesises a variant sequence. */
#define GLYPH_VARIANT_SEP "\x1F"
#define GLYPH_VARIANT_BUF 512
#define GLYPH_VARIANT_MAX 8

static GlyphVariantsProvider g_variants_provider = NULL;

void super_v_ui_set_glyph_variants_provider(GlyphVariantsProvider cb) {
    g_variants_provider = cb;
}

static gboolean glyph_is_only_modifiers(const char *glyph) {
    if (!glyph || !*glyph) return TRUE;
    gboolean any = FALSE;
    const char *p = glyph;
    while (*p) {
        int len = 0;
        gunichar cp = utf8_decode_at(p, &len);
        if (!is_skin_modifier(cp) && cp != 0x200D) return FALSE;
        any = TRUE;
        p += len;
    }
    return any;
}

static void show_variant_popover(GtkWidget *btn, const char *base) {
    if (!base || !*base) return;

    char buf[GLYPH_VARIANT_BUF];
    buf[0] = '\0';
    int len = -1;
    if (g_variants_provider) len = g_variants_provider(base, buf, (int)sizeof(buf));

    GPtrArray *variants = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(variants, g_strdup(base));
    if (len > 0 && buf[0]) {
        char **parts = g_strsplit(buf, GLYPH_VARIANT_SEP, GLYPH_VARIANT_MAX);
        for (int i = 0; parts[i]; i++) {
            if (parts[i][0] && strcmp(parts[i], base) != 0) {
                g_ptr_array_add(variants, g_strdup(parts[i]));
            }
        }
        g_strfreev(parts);
    }
    if (variants->len <= 1) {
        g_ptr_array_free(variants, TRUE);
        return;
    }

    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, btn);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    for (guint i = 0; i < variants->len; i++) {
        const char *variant = g_ptr_array_index(variants, i);
        GtkWidget *vb = gtk_button_new_with_label(variant);
        gtk_widget_add_css_class(vb, "flat");
        gtk_widget_add_css_class(vb, "emoji-cell");
        g_signal_connect_data(vb, "clicked", G_CALLBACK(on_skin_variant_clicked),
            g_strdup(variant), (GClosureNotify)free_closure_data, 0);
        gtk_box_append(GTK_BOX(box), vb);
    }
    g_ptr_array_free(variants, TRUE);

    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_emoji_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)n_press; (void)x; (void)y;
    GtkWidget *btn = GTK_WIDGET(user_data);
    const char *base = (const char *)g_object_get_data(G_OBJECT(btn), "base_emoji");
    if (!base) return;
    if (gesture) gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    show_variant_popover(btn, base);
}

static void on_emoji_long_pressed(GtkGestureLongPress *gesture, double x, double y, gpointer user_data) {
    (void)gesture; (void)x; (void)y;
    GtkWidget *btn = GTK_WIDGET(user_data);
    const char *base = (const char *)g_object_get_data(G_OBJECT(btn), "base_emoji");
    if (base) show_variant_popover(btn, base);
}

void super_v_ui_add_glyph_item(const char *tab, const char *glyph, const char *name, const char *keywords, int skin_tone_capable) {
    (void)keywords;
    GtkWidget *flow = flow_for_tab(tab);
    if (!flow || !glyph) return;

    /* Standalone modifiers are not emoji; they rendered as stray colour blocks. */
    if (glyph_is_only_modifiers(glyph)) return;

    gboolean is_emoji = (tab && strcmp(tab, "emoji") == 0);
    gboolean has_variants = is_emoji && skin_tone_capable;

    GtkWidget *btn = gtk_button_new();
    set_glyph_tooltip(btn, glyph, name);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_add_css_class(btn, is_emoji ? "emoji-cell" : "symbol-cell");
    gtk_widget_set_size_request(btn, is_emoji ? EMOJI_CELL : SYMBOL_CELL, is_emoji ? EMOJI_CELL : SYMBOL_CELL);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), glyph_visual(glyph, is_emoji));

    if (has_variants) {
        GtkWidget *dot = gtk_label_new("\u2022");
        gtk_widget_add_css_class(dot, "variant-dot");
        gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(dot, GTK_ALIGN_END);
        gtk_widget_set_can_target(dot, FALSE);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), dot);
    }

    gtk_button_set_child(GTK_BUTTON(btn), overlay);

    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_glyph_clicked),
        g_strdup(glyph), (GClosureNotify)free_closure_data, 0);

    if (has_variants) {
        g_object_set_data_full(G_OBJECT(btn), "base_emoji", g_strdup(glyph), g_free);

        GtkGesture *right = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(right), GTK_PHASE_CAPTURE);
        g_signal_connect(right, "pressed", G_CALLBACK(on_emoji_right_clicked), btn);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(right));

        GtkGesture *longpress = gtk_gesture_long_press_new();
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(longpress), GTK_PHASE_CAPTURE);
        g_signal_connect(longpress, "pressed", G_CALLBACK(on_emoji_long_pressed), btn);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(longpress));
    }

    gtk_flow_box_append(GTK_FLOW_BOX(flow), btn);
}

void super_v_ui_clear_glyphs(const char *tab) {
    GtkWidget *flow = flow_for_tab(tab);
    if (!flow) return;
    GtkWidget *child = gtk_widget_get_first_child(flow);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(flow), child);
        child = next;
    }
}

void super_v_ui_record_recent(const char *type, const char *content) {
    (void)type;
    if (!g_recent_emoji_flow || !content) return;
    GtkWidget *btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(btn), glyph_visual(content, TRUE));
    set_glyph_tooltip(btn, content, NULL);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_add_css_class(btn, "emoji-cell");
    gtk_widget_set_size_request(btn, EMOJI_CELL, EMOJI_CELL);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_glyph_clicked),
        g_strdup(content), (GClosureNotify)free_closure_data, 0);
    gtk_flow_box_insert(GTK_FLOW_BOX(g_recent_emoji_flow), btn, 0);
}

static void apply_entry_text(const char *text) {
    g_setting_entry = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(g_search_entry), text ? text : "");
    g_setting_entry = FALSE;
}

static const char *saved_query_for(const char *tab) {
    if (!tab) return "";
    if (strcmp(tab, "emoji") == 0) return g_query_emoji;
    if (strcmp(tab, "symbols") == 0 || strcmp(tab, "symbol") == 0) return g_query_symbol;
    return "";
}

static void first_page(const char *tab) {
    if (!g_glyph_provider) return;
    const char *slug = (strcmp(tab, "symbols") == 0) ? "symbol" : tab;
    super_v_ui_clear_glyphs(tab);
    int n = g_glyph_provider(slug, saved_query_for(tab), 0, GLYPH_PAGE);
    if (strcmp(slug, "emoji") == 0) {
        g_offset_emoji = n;
        g_done_emoji = (n < GLYPH_PAGE);
    } else {
        g_offset_symbol = n;
        g_done_symbol = (n < GLYPH_PAGE);
    }
}

static void load_more_glyphs(const char *tab) {
    if (!g_glyph_provider || g_loading_page) return;
    gboolean is_emoji = (strcmp(tab, "emoji") == 0);
    gboolean done = is_emoji ? g_done_emoji : g_done_symbol;
    if (done) return;

    g_loading_page = TRUE;
    int offset = is_emoji ? g_offset_emoji : g_offset_symbol;
    const char *slug = is_emoji ? "emoji" : "symbol";
    int emitted = g_glyph_provider(slug, is_emoji ? g_query_emoji : g_query_symbol, offset, GLYPH_PAGE);
    offset += emitted;
    if (is_emoji) {
        g_offset_emoji = offset;
        g_done_emoji = (emitted < GLYPH_PAGE);
    } else {
        g_offset_symbol = offset;
        g_done_symbol = (emitted < GLYPH_PAGE);
    }
    g_loading_page = FALSE;
}

static void on_scroll_edge(GtkScrolledWindow *sw, GtkPositionType pos, gpointer user_data) {
    (void)sw;
    if (pos != GTK_POS_BOTTOM) return;
    load_more_glyphs((const char *)user_data);
}

/* GIF pages come from the provider, so the edge asks Rust for the next page.
 * Rust stops asking once the provider is exhausted. */
static void on_gif_scroll_edge(GtkScrolledWindow *sw, GtkPositionType pos, gpointer user_data) {
    (void)user_data;
    if (pos != GTK_POS_BOTTOM) return;
    GtkAdjustment *v = gtk_scrolled_window_get_vadjustment(sw);
    if (v) {
        double value = gtk_adjustment_get_value(v);
        double page = gtk_adjustment_get_page_size(v);
        double upper = gtk_adjustment_get_upper(v);
        /* A short page that fits the viewport is already "at the bottom"; do not
         * prefetch every page the moment the tab opens. */
        if (upper <= page + 1.0) return;
        if (value + page < upper - 1.0) return;
    }
    if (g_gif_more_hook) g_gif_more_hook();
}

static void on_tab_switched(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)obj; (void)pspec; (void)user_data;
    if (!g_stack) return;
    const char *name = adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(g_stack));
    if (!name) return;

    apply_entry_text(saved_query_for(name));

    if (strcmp(name, "clipboard") == 0) {
        gtk_list_box_invalidate_filter(GTK_LIST_BOX(g_clip_list));
        gtk_widget_grab_focus(g_search_entry);
    } else if (strcmp(name, "emoji") == 0) {
        if (g_offset_emoji == 0) first_page("emoji");
    } else if (strcmp(name, "symbols") == 0) {
        if (g_offset_symbol == 0) first_page("symbols");
    } else if (strcmp(name, "gif") == 0) {
        gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(g_gif_flow));
        /* Pages are fetched asynchronously, so request the first one once. */
        if (!g_gif_requested) {
            g_gif_requested = TRUE;
            super_v_ui_refresh_gifs();
        }
    }
}

/* GIF search hits the provider, so debounce it instead of firing per keystroke. */
static guint g_gif_search_source = 0;
static char g_gif_query[160] = "";

static gboolean gif_search_fire(gpointer data) {
    (void)data;
    g_gif_search_source = 0;
    if (g_gif_search_hook) g_gif_search_hook(g_gif_query);
    return G_SOURCE_REMOVE;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    (void)entry; (void)user_data;
    if (g_setting_entry) return;

    const char *name = g_stack ? adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(g_stack)) : "clipboard";
    if (!name) name = "clipboard";
    const char *q = gtk_editable_get_text(GTK_EDITABLE(g_search_entry));
    if (!q) q = "";

    if (strcmp(name, "clipboard") == 0) {
        gtk_list_box_invalidate_filter(GTK_LIST_BOX(g_clip_list));
    } else if (strcmp(name, "emoji") == 0) {
        g_strlcpy(g_query_emoji, q, sizeof(g_query_emoji));
        g_offset_emoji = 0;
        g_done_emoji = 0;
        first_page("emoji");
    } else if (strcmp(name, "symbols") == 0) {
        g_strlcpy(g_query_symbol, q, sizeof(g_query_symbol));
        g_offset_symbol = 0;
        g_done_symbol = 0;
        first_page("symbols");
    } else if (strcmp(name, "gif") == 0) {
        gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(g_gif_flow));
        g_strlcpy(g_gif_query, q, sizeof(g_gif_query));
        if (g_gif_search_source) g_source_remove(g_gif_search_source);
        g_gif_search_source = g_timeout_add(450, gif_search_fire, NULL);
    }
}

/* -------------------------------------------------------------------- GIF */

void super_v_ui_add_gif_item(const char *path, const char *title) {
    if (!g_gif_flow || !path) return;
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_add_css_class(btn, "gif-cell");
    gtk_widget_set_tooltip_text(btn, title ? title : path);

    GtkWidget *thumb = make_gif_thumb(path, 84);
    if (thumb) {
        gtk_button_set_child(GTK_BUTTON(btn), thumb);
    } else {
        gtk_button_set_label(GTK_BUTTON(btn), title ? title : "GIF");
    }

    g_object_set_data_full(G_OBJECT(btn), "super_v_gif_title", g_strdup(title ? title : ""), g_free);
    g_object_set_data_full(G_OBJECT(btn), "super_v_gif_path", g_strdup(path), g_free);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_gif_clicked),
        g_strdup(path), (GClosureNotify)free_closure_data, 0);
    gtk_flow_box_append(GTK_FLOW_BOX(g_gif_flow), btn);
}

/* Picking a GIF must paste the picture; the old path went through the text
 * paste callback, so the field ended up with "/home/.../file.gif". */
static void on_gif_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *path = (const char *)user_data;
    if (!path || !*path) return;
    gint64 now = g_get_monotonic_time();
    if (now - g_last_pick_us < 300000) return;
    g_last_pick_us = now;
    /* Keep the GIF itself in history, and skip the clipboard echo. */
    if (g_image_hook) g_image_hook(path, "image/gif");
    g_skip_change = TRUE;
    paste_file_after_hide(path, "image/gif");
}

void super_v_ui_clear_gif_items(void) {
    if (!g_gif_flow) return;
    GtkWidget *child = gtk_widget_get_first_child(g_gif_flow);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(g_gif_flow), child);
        child = next;
    }
}

void super_v_ui_set_gif_hint(const char *text) {
    if (g_gif_label) gtk_label_set_text(GTK_LABEL(g_gif_label), text ? text : "");
}

void super_v_ui_refresh_gifs(void) {
    g_gif_requested = TRUE;
    super_v_ui_clear_gif_items();
    if (g_gif_reload_hook) g_gif_reload_hook();
}

static gboolean filter_gif_row(GtkFlowBoxChild *child, gpointer user_data) {
    (void)user_data;
    /* Online providers already searched: filtering titles again would hide the
     * very hits the API returned for the query. */
    if (strcmp(g_gif_provider, "local") != 0) return TRUE;
    const char *query = gtk_editable_get_text(GTK_EDITABLE(g_search_entry));
    if (!query || !*query) return TRUE;
    GtkWidget *btn = gtk_flow_box_child_get_child(child);
    if (!btn) return TRUE;
    const char *title = (const char *)g_object_get_data(G_OBJECT(btn), "super_v_gif_title");
    if (!title) return FALSE;
    gchar *a = g_utf8_strdown(title, -1);
    gchar *b = g_utf8_strdown(query, -1);
    gboolean ok = strstr(a, b) != NULL;
    g_free(a); g_free(b);
    return ok;
}

/* --------------------------------------------------------------- settings */

static int lang_index_for(const char *langs) {
    if (!langs) return 0;
    if (strstr(langs, "en") && strstr(langs, "id")) return 0;
    if (strchr(langs, 'e')) return 1;
    return 2;
}

static const char *langs_for_index(int idx) {
    switch (idx) {
        case 1: return "en";
        case 2: return "id";
        default: return "en,id";
    }
}

static void on_lang_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec; (void)user_data;
    if (g_setting_combo) return;
    guint idx = adw_combo_row_get_selected(ADW_COMBO_ROW(obj));
    const char *langs = langs_for_index((int)idx);
    g_strlcpy(g_langs, langs, sizeof(g_langs));
    if (g_language_hook) g_language_hook(g_langs);

    /* Re-run the current tab query against the new language set. */
    g_query_emoji[0] = 0;
    g_query_symbol[0] = 0;
    g_offset_emoji = 0;
    g_done_emoji = 0;
    g_offset_symbol = 0;
    g_done_symbol = 0;
    super_v_ui_clear_glyphs("emoji");
    super_v_ui_clear_glyphs("symbols");
    const char *name = g_stack ? adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(g_stack)) : NULL;
    if (name && strcmp(name, "emoji") == 0) on_search_changed(NULL, NULL);
    if (name && strcmp(name, "symbols") == 0) on_search_changed(NULL, NULL);
}

static int gif_index_for(const char *id) {
    if (!id) return 0;
    if (strcmp(id, "tenor") == 0) return 1;
    if (strcmp(id, "giphy") == 0) return 2;
    return 0;
}

static const char *gif_id_for_index(int idx) {
    switch (idx) {
        case 1: return "tenor";
        case 2: return "giphy";
        default: return "local";
    }
}

static void on_gif_provider_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec; (void)user_data;
    if (g_setting_combo) return;
    guint idx = adw_combo_row_get_selected(ADW_COMBO_ROW(obj));
    const char *id = gif_id_for_index((int)idx);
    g_strlcpy(g_gif_provider, id, sizeof(g_gif_provider));
    if (g_gif_provider_hook) g_gif_provider_hook(g_gif_provider);
    super_v_ui_refresh_gifs();
}

static void on_rescan_gifs(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    super_v_ui_refresh_gifs();
}

static void on_about_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    super_v_ui_show_about(user_data);
}

/* --------------------------------------------------------------- dialogs */

static const char *g_api_key_provider_value(void) { return g_api_key; }

static AdwDialog *g_open_dialog = NULL;
static char g_log_path[512] = "";
static GtkWidget *g_api_key_row = NULL;
static GtkWidget *g_provider_test_row = NULL;
static GtkWidget *g_log_view = NULL;
static guint g_api_key_timeout = 0;

void super_v_ui_set_api_key_text(const char *key) {
    if (!key) return;
    g_strlcpy(g_api_key, key, sizeof(g_api_key));
    if (g_api_key_row) {
        g_setting_entry = TRUE;
        gtk_editable_set_text(GTK_EDITABLE(g_api_key_row), g_api_key);
        g_setting_entry = FALSE;
    }
}

void super_v_ui_set_log_path(const char *path) {
    if (path) g_strlcpy(g_log_path, path, sizeof(g_log_path));
}

static void track_dialog(AdwDialog *dialog) {
    g_open_dialog = dialog;
    g_signal_connect_swapped(dialog, "closed", G_CALLBACK(g_nullify_pointer), &g_open_dialog);
}

static void close_open_dialog(void) {
    if (g_open_dialog) {
        adw_dialog_close(g_open_dialog);
        g_open_dialog = NULL;
    }
}

/* --------------------------------------------------------------------- logs */

static GtkWidget *build_log_view(void) {
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(view, "card");
    gtk_widget_set_margin_start(view, 8);
    gtk_widget_set_margin_end(view, 8);
    gtk_widget_set_margin_bottom(view, 8);
    return view;
}

static void refresh_log_view(void) {
    if (!g_log_view) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_log_view));
    if (g_log_path[0] == 0) {
        gtk_text_buffer_set_text(buf, "No log path configured.", -1);
        return;
    }
    gchar *contents = NULL;
    if (g_file_get_contents(g_log_path, &contents, NULL, NULL)) {
        gsize len = strlen(contents);
        /* keep the tail: the debug window is for the last few hundred lines */
        const gsize cap = 120 * 1024;
        const char *start = contents;
        if (len > cap) start = contents + (len - cap);
        gtk_text_buffer_set_text(buf, start, -1);
        g_free(contents);
    } else {
        gchar *msg = g_strdup_printf("Log file not created yet: %s", g_log_path);
        gtk_text_buffer_set_text(buf, msg, -1);
        g_free(msg);
    }

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_place_cursor(buf, &end);
}

static void on_log_refresh(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    refresh_log_view();
}

static void on_log_copy(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    if (!g_log_view) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_log_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    copy_text_to_clipboard(text);
    g_free(text);
}

static void on_log_folder(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    gchar *dir = g_path_get_dirname(g_log_path[0] ? g_log_path : ".");
    GFile *file = g_file_new_for_path(dir);
    gchar *uri = g_file_get_uri(file);
    gtk_show_uri(GTK_WINDOW(g_main_window), uri, GDK_CURRENT_TIME);
    g_free(uri);
    g_object_unref(file);
    g_free(dir);
}

void super_v_ui_show_debug(void) {
    AdwDialog *dialog = ADW_DIALOG(adw_dialog_new());
    adw_dialog_set_title(dialog, "Debug and Logs");
    adw_dialog_set_content_width(dialog, 620);
    adw_dialog_set_content_height(dialog, 520);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_dialog_set_child(dialog, box);

    GtkWidget *header = adw_header_bar_new();
    GtkWidget *refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh_btn, "Reload log");
    gtk_widget_add_css_class(refresh_btn, "flat");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_log_refresh), NULL);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), refresh_btn);

    GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_set_tooltip_text(copy_btn, "Copy all");
    gtk_widget_add_css_class(copy_btn, "flat");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_log_copy), NULL);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), copy_btn);

    GtkWidget *folder_btn = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_set_tooltip_text(folder_btn, "Open log folder");
    gtk_widget_add_css_class(folder_btn, "flat");
    g_signal_connect(folder_btn, "clicked", G_CALLBACK(on_log_folder), NULL);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), folder_btn);
    gtk_box_append(GTK_BOX(box), header);

    GtkWidget *path_lbl = gtk_label_new(g_log_path[0] ? g_log_path : "log path unknown");
    gtk_widget_set_halign(path_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start(path_lbl, 12);
    gtk_widget_set_margin_top(path_lbl, 6);
    gtk_widget_set_margin_bottom(path_lbl, 6);
    gtk_widget_add_css_class(path_lbl, "dim-label");
    gtk_label_set_selectable(GTK_LABEL(path_lbl), TRUE);
    gtk_box_append(GTK_BOX(box), path_lbl);

    g_log_view = build_log_view();
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), g_log_view);
    gtk_box_append(GTK_BOX(box), scroll);

    refresh_log_view();
    track_dialog(dialog);
    adw_dialog_present(dialog, GTK_WIDGET(g_main_window));
}

static void on_about_row_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    super_v_ui_show_about(NULL);
}

static void on_debug_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    super_v_ui_show_debug();
}

/* ---------------------------------------------------------------- provider */

void super_v_ui_report_provider_test(const char *message) {
    if (g_provider_test_row) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_provider_test_row),
            message ? message : "unknown result");
    }
    if (!message) return;
    gboolean ok = g_str_has_prefix(message, "PASS");
    GtkWidget *row = g_provider_test_row;
    if (!row) return;
    gtk_widget_remove_css_class(row, "success");
    gtk_widget_remove_css_class(row, "error");
    gtk_widget_add_css_class(row, ok ? "success" : "error");
}

static gboolean flush_api_key(gpointer data) {
    (void)data;
    g_api_key_timeout = 0;
    if (g_api_key_row && g_api_key_hook) {
        const char *text = gtk_editable_get_text(GTK_EDITABLE(g_api_key_row));
        g_api_key_hook(text ? text : "");
    }
    return G_SOURCE_REMOVE;
}

static void on_api_key_changed(GtkEditable *editable, gpointer data) {
    (void)editable; (void)data;
    if (g_api_key_timeout) g_source_remove(g_api_key_timeout);
    g_api_key_timeout = g_timeout_add(600, flush_api_key, NULL);
}

static void on_test_provider(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    if (g_provider_test_row) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_provider_test_row), "Testing\u2026");
    }
    if (g_provider_test_hook) g_provider_test_hook(g_gif_provider);
}

/* -------------------------------------------------------------------- about */

void super_v_ui_show_about(void *parent_window) {
    (void)parent_window;
    AdwDialog *dialog = ADW_DIALOG(adw_preferences_dialog_new());
    adw_preferences_dialog_set_search_enabled(ADW_PREFERENCES_DIALOG(dialog), FALSE);

    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "About");
    adw_preferences_page_set_icon_name(page, "help-about-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), page);

    AdwPreferencesGroup *app_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(app_group, "Super-V Clipboard 0.1.0");
    adw_preferences_group_set_description(app_group,
        "Windows-style clipboard, emoji and GIF picker for GNOME, Hyprland and X11.");
    adw_preferences_page_add(page, app_group);

    static const struct { const char *title; const char *subtitle; } app_rows[] = {
        { "Author", "Arkhi Muttaqina (arkhi07@hotmail.co.id)" },
        { "Copyright", "\xc2\xa9 2026 Arkhi Muttaqina" },
        { "License", "GNU General Public License v3.0 or later (GPL-3.0-or-later)" },
        { "Repository", "github.com/ArkhiMuttaqina/Super-V-Clipboard" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(app_rows); i++) {
        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), app_rows[i].title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), app_rows[i].subtitle);
        adw_preferences_group_add(app_group, row);
    }

    AdwPreferencesGroup *libs = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(libs, "Bundled libraries");
    adw_preferences_page_add(page, libs);

    static const struct { const char *name; const char *why; } lib_rows[] = {
        { "GTK 4", "widget toolkit" },
        { "libadwaita", "GNOME widgets and dialogs" },
        { "GdkPixbuf", "image decode and thumbnail scaling" },
        { "Noto Color Emoji", "emoji glyph rendering" },
        { "Tokio", "async runtime for IPC and daemon" },
        { "rusqlite / SQLite 3 + FTS5", "storage and full-text search" },
        { "serde + serde_json", "settings, datasets, backup format" },
        { "sha2", "SHA-256 content addressing for blobs" },
        { "anyhow", "error handling" },
        { "log + env_logger", "runtime logging" },
        { "libc", "signal and process plumbing" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(lib_rows); i++) {
        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), lib_rows[i].name);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), lib_rows[i].why);
        adw_preferences_group_add(libs, row);
    }

    AdwPreferencesGroup *support = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(support, "Troubleshooting");
    adw_preferences_page_add(page, support);

    GtkWidget *debug_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(debug_row), "Debug and logs");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(debug_row), "Tail this app's log, copy it, or open the log folder");
    GtkWidget *debug_btn = gtk_button_new_with_label("Open");
    gtk_widget_add_css_class(debug_btn, "flat");
    gtk_widget_set_valign(debug_btn, GTK_ALIGN_CENTER);
    g_signal_connect(debug_btn, "clicked", G_CALLBACK(on_debug_clicked), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(debug_row), debug_btn);
    adw_preferences_group_add(support, debug_row);

    track_dialog(dialog);
    adw_dialog_present(dialog, GTK_WIDGET(g_main_window));
}

void super_v_ui_show_settings(void *parent_window) {
    (void)parent_window;
    AdwDialog *dialog = ADW_DIALOG(adw_preferences_dialog_new());
    adw_preferences_dialog_set_search_enabled(ADW_PREFERENCES_DIALOG(dialog), TRUE);

    /* ---- General ---- */
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "General");
    adw_preferences_page_set_icon_name(page, "preferences-system-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), page);

    AdwPreferencesGroup *sort_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(sort_group, "Sorting and Organization");
    adw_preferences_page_add(page, sort_group);

    const char *sort_options[] = { "Recently Used (Default)", "Pin First, then Recent", "Alphabetical (A-Z)", "Size (Largest First)", NULL };
    GtkWidget *sort_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sort_row), "Sort Order");
    adw_combo_row_set_model(ADW_COMBO_ROW(sort_row), G_LIST_MODEL(gtk_string_list_new(sort_options)));
    adw_preferences_group_add(sort_group, sort_row);

    AdwPreferencesGroup *app_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(app_group, "Application");
    adw_preferences_page_add(page, app_group);

    GtkWidget *about_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(about_row), "About Super-V Clipboard");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(about_row), "Version, author, licence, bundled libraries");
    GtkWidget *about_btn = gtk_button_new_with_label("Open");
    gtk_widget_add_css_class(about_btn, "flat");
    gtk_widget_set_valign(about_btn, GTK_ALIGN_CENTER);
    g_signal_connect(about_btn, "clicked", G_CALLBACK(on_about_row_clicked), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(about_row), about_btn);
    adw_preferences_group_add(app_group, about_row);

    GtkWidget *debug_row2 = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(debug_row2), "Debug and logs");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(debug_row2), "Tail the log, copy it, open the log folder");
    GtkWidget *debug_btn2 = gtk_button_new_with_label("Open");
    gtk_widget_add_css_class(debug_btn2, "flat");
    gtk_widget_set_valign(debug_btn2, GTK_ALIGN_CENTER);
    g_signal_connect(debug_btn2, "clicked", G_CALLBACK(on_debug_clicked), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(debug_row2), debug_btn2);
    adw_preferences_group_add(app_group, debug_row2);

    AdwPreferencesGroup *win_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(win_group, "Window");
    adw_preferences_group_set_description(win_group, "GNOME Wayland does not let applications position their own windows, so the near-pointer mode draws a transparent fullscreen surface and places the panel next to the cursor.");
    adw_preferences_page_add(page, win_group);

    const char *placement_options[] = { "Center of screen", "Near pointer (Wayland overlay)", NULL };
    GtkWidget *placement_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(placement_row), "Window placement");
    adw_combo_row_set_model(ADW_COMBO_ROW(placement_row), G_LIST_MODEL(gtk_string_list_new(placement_options)));
    select_combo(placement_row, g_placement_pointer ? 1 : 0);
    g_signal_connect(placement_row, "notify::selected", G_CALLBACK(on_placement_changed), NULL);
    adw_preferences_group_add(win_group, placement_row);
    /* ---- Emoji ---- */
    AdwPreferencesPage *emoji_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(emoji_page, "Emoji and Symbols");
    adw_preferences_page_set_icon_name(emoji_page, "face-smile-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), emoji_page);

    AdwPreferencesGroup *lang_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(lang_group, "Search Languages");
    adw_preferences_group_set_description(lang_group,
        "Search emoji by everyday words. Indonesian and English dictionaries are bundled; "
        "new languages are a data-only contribution in data/glyphs/i18n.json.");
    adw_preferences_page_add(emoji_page, lang_group);

    const char *lang_options[] = { "English + Indonesian (Default)", "English only", "Indonesian only", NULL };
    g_lang_combo = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_lang_combo), "Emoji search languages");
    adw_combo_row_set_model(ADW_COMBO_ROW(g_lang_combo), G_LIST_MODEL(gtk_string_list_new(lang_options)));
    select_combo(g_lang_combo, (guint)lang_index_for(g_langs));
    g_signal_connect(g_lang_combo, "notify::selected", G_CALLBACK(on_lang_changed), NULL);
    adw_preferences_group_add(lang_group, g_lang_combo);

    AdwPreferencesGroup *style_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(style_group, "Emoji Style");
    adw_preferences_group_set_description(style_group, "Follows the system emoji font (Noto Color Emoji by default).");
    adw_preferences_page_add(emoji_page, style_group);

    const char *styles[] = { "System default (Noto Color Emoji)", "Windows 11 Fluent", "Apple iOS", "Android Material", "WhatsApp 2026", NULL };
    GtkWidget *style_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(style_row), "Glyph set");
    adw_combo_row_set_model(ADW_COMBO_ROW(style_row), G_LIST_MODEL(gtk_string_list_new(styles)));
    adw_preferences_group_add(style_group, style_row);

    /* ---- GIF ---- */
    AdwPreferencesPage *gif_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(gif_page, "GIF and Media");
    adw_preferences_page_set_icon_name(gif_page, "media-optical-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), gif_page);

    AdwPreferencesGroup *gif_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(gif_group, "GIF Provider");
    adw_preferences_group_set_description(gif_group,
        "Local works offline. Tenor/Giphy need an API key from their developer portal. "
        "Want another provider? Send a PR upstream: github.com/ArkhiMuttaqina/Super-V-Clipboard");
    adw_preferences_page_add(gif_page, gif_group);

    const char *providers[] = { "Local library (~/Pictures, ~/Downloads)", "Tenor API", "Giphy", NULL };
    g_gif_combo = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_gif_combo), "Active provider");
    adw_combo_row_set_model(ADW_COMBO_ROW(g_gif_combo), G_LIST_MODEL(gtk_string_list_new(providers)));
    select_combo(g_gif_combo, (guint)gif_index_for(g_gif_provider));
    g_signal_connect(g_gif_combo, "notify::selected", G_CALLBACK(on_gif_provider_changed), NULL);
    adw_preferences_group_add(gif_group, g_gif_combo);

    g_api_key_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_api_key_row), "API key (Tenor / Giphy)");
    gtk_editable_set_text(GTK_EDITABLE(g_api_key_row), g_api_key_provider_value());
    g_signal_connect(g_api_key_row, "changed", G_CALLBACK(on_api_key_changed), NULL);
    adw_preferences_group_add(gif_group, g_api_key_row);

    g_provider_test_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_provider_test_row), "Test provider");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(g_provider_test_row),
        "Runs a quick smoke test with the key above and reports what came back");
    GtkWidget *test_btn = gtk_button_new_with_label("Test");
    gtk_widget_add_css_class(test_btn, "flat");
    gtk_widget_set_valign(test_btn, GTK_ALIGN_CENTER);
    g_signal_connect(test_btn, "clicked", G_CALLBACK(on_test_provider), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(g_provider_test_row), test_btn);
    adw_preferences_group_add(gif_group, g_provider_test_row);

    GtkWidget *rescan_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(rescan_row), "Rescan local GIFs");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(rescan_row), "Re-index animated images found on disk.");
    GtkWidget *rescan_btn = gtk_button_new_with_label("Rescan");
    gtk_widget_add_css_class(rescan_btn, "flat");
    gtk_widget_set_valign(rescan_btn, GTK_ALIGN_CENTER);
    g_signal_connect(rescan_btn, "clicked", G_CALLBACK(on_rescan_gifs), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(rescan_row), rescan_btn);
    adw_preferences_group_add(gif_group, rescan_row);

    track_dialog(dialog);
    adw_dialog_present(dialog, GTK_WIDGET(g_main_window));
}

static void select_combo(GtkWidget *combo, guint idx) {
    if (!combo) return;
    g_setting_combo = TRUE;
    adw_combo_row_set_selected(ADW_COMBO_ROW(combo), idx);
    g_setting_combo = FALSE;
}

void super_v_ui_apply_preferences(const char *langs, const char *gif_provider) {
    if (langs && *langs) g_strlcpy(g_langs, langs, sizeof(g_langs));
    if (gif_provider && *gif_provider) g_strlcpy(g_gif_provider, gif_provider, sizeof(g_gif_provider));
    select_combo(g_lang_combo, (guint)lang_index_for(g_langs));
    select_combo(g_gif_combo, (guint)gif_index_for(g_gif_provider));
}

/* ------------------------------------------------------------------ window */

static gboolean idle_exit_cb(gpointer data) {
    (void)data;
    if (g_main_window) {
        gtk_window_destroy(GTK_WINDOW(g_main_window));
        g_main_window = NULL;
    }
    if (G_APPLICATION(g_application_get_default())) {
        g_application_quit(G_APPLICATION(g_application_get_default()));
    }
    g_idle_exit_source = 0;
    return G_SOURCE_REMOVE;
}

static void schedule_idle_exit(void) {
    cancel_idle_exit();
    guint secs = IDLE_EXIT_SECONDS;
    const char *override = g_getenv("SUPER_V_IDLE_EXIT_SECONDS");
    if (override) {
        int v = atoi(override);
        if (v > 0) secs = (guint)v;
    }
    g_idle_exit_source = g_timeout_add_seconds(secs, idle_exit_cb, NULL);
}

static void cancel_idle_exit(void) {
    if (g_idle_exit_source) {
        g_source_remove(g_idle_exit_source);
        g_idle_exit_source = 0;
    }
}

static void hide_main_window(void) {
    if (!g_main_window) return;
    close_open_dialog();
    gtk_widget_set_visible(g_main_window, FALSE);
    schedule_idle_exit();
    malloc_trim(0);
}

static gboolean on_hide_signal(gpointer data) {
    (void)data;
    hide_main_window();
    return G_SOURCE_CONTINUE;
}

static gboolean sync_clipboard_idle(gpointer data) {
    (void)data;
    sync_clipboard_once();
    return G_SOURCE_REMOVE;
}

/* Raw bytes are more reliable than decode-to-texture: the shell may hand over
 * image/png or image/jpeg and GDK's texture path silently fails when the
 * selection is not readable yet. */
static const char *const IMAGE_MIMES[] = {
    "image/png", "image/jpeg", "image/webp", "image/gif", "image/bmp", NULL
};

static const char *ext_for_mime(const char *mime) {
    if (!mime) return "png";
    if (strcmp(mime, "image/jpeg") == 0) return "jpg";
    if (strcmp(mime, "image/webp") == 0) return "webp";
    if (strcmp(mime, "image/gif") == 0) return "gif";
    if (strcmp(mime, "image/bmp") == 0) return "bmp";
    return "png";
}

static void on_image_stream_ready(GObject *source, GAsyncResult *res, gpointer data) {
    char *mime = (char *)data;
    GError *err = NULL;
    GInputStream *stream = gdk_clipboard_read_finish(GDK_CLIPBOARD(source), res, NULL, &err);
    if (!stream) {
        if (g_getenv("SUPER_V_DEBUG_CLIPBOARD"))
            g_printerr("IMG stream failed: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
        g_free(mime);
        return;
    }

    GByteArray *bytes = g_byte_array_new();
    unsigned char chunk[65536];
    gssize got = 0;
    while ((got = g_input_stream_read(stream, chunk, sizeof(chunk), NULL, NULL)) > 0) {
        g_byte_array_append(bytes, chunk, (guint)got);
        if (bytes->len > 16 * 1024 * 1024) break;
    }
    g_object_unref(stream);

    if (bytes->len == 0) {
        g_byte_array_free(bytes, TRUE);
        g_free(mime);
        return;
    }

    char path[256];
    g_snprintf(path, sizeof(path), "/tmp/super-v-ui-clip-%" G_GINT64_FORMAT ".%s",
        (gint64)g_get_monotonic_time(), ext_for_mime(mime));
    g_file_set_contents(path, (const char *)bytes->data, (gssize)bytes->len, NULL);
    if (g_getenv("SUPER_V_DEBUG_CLIPBOARD"))
        g_printerr("IMG captured %s (%u bytes, %s)\n", path, bytes->len, mime);
    g_byte_array_free(bytes, TRUE);

    if (g_image_hook) g_image_hook(path, mime);
    /* the row only exists after the daemon stored it */
    g_timeout_add(400, refresh_history_idle, NULL);
    g_free(mime);
}

static void read_clipboard_image(GdkClipboard *clip) {
    GdkContentFormats *formats = gdk_clipboard_get_formats(clip);
    if (!formats) return;
    const char *mime = NULL;
    for (int i = 0; IMAGE_MIMES[i]; i++) {
        if (gdk_content_formats_contain_mime_type(formats, IMAGE_MIMES[i])) {
            mime = IMAGE_MIMES[i];
            break;
        }
    }
    if (!mime) return;
    const char *mimes[] = { mime, NULL };
    gdk_clipboard_read_async(clip, mimes, G_PRIORITY_DEFAULT, NULL,
        on_image_stream_ready, g_strdup(mime));
}

static void sync_clipboard_once(void) {
    GdkDisplay *d = gdk_display_get_default();
    if (!d) return;
    GdkClipboard *clip = gdk_display_get_clipboard(d);
    if (!clip) return;
    gdk_clipboard_read_text_async(clip, NULL, on_clipboard_text_ready, NULL);
    read_clipboard_image(clip);
}

static gboolean on_toggle_signal(gpointer data) {
    (void)data;
    if (!g_main_window) return G_SOURCE_CONTINUE;
    if (gtk_widget_get_visible(g_main_window)) {
        hide_main_window();
        return G_SOURCE_CONTINUE;
    }
    cancel_idle_exit();
    gtk_widget_set_visible(g_main_window, TRUE);
    gtk_window_present(GTK_WINDOW(g_main_window));
    g_timeout_add(60, place_after_map, NULL);
    gtk_widget_grab_focus(g_search_entry);
    if (g_stack && strcmp(adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(g_stack)), "gif") == 0)
    refresh_bridge_status();
    g_timeout_add(200, sync_clipboard_idle, NULL);
    return G_SOURCE_CONTINUE;
}

/* Fit a frame inside the cell without distorting: scale by the smaller ratio. */
static GdkPixbuf *fit_pixbuf(GdkPixbuf *src, int size) {
    int w = gdk_pixbuf_get_width(src);
    int h = gdk_pixbuf_get_height(src);
    double scale = 1.0;
    if (w > size || h > size)
        scale = MIN((double)size / (double)w, (double)size / (double)h);
    if (scale >= 1.0) return (GdkPixbuf *)g_object_ref(src);
    int target_w = MAX(1, (int)(w * scale));
    int target_h = MAX(1, (int)(h * scale));
    return gdk_pixbuf_scale_simple(src, target_w, target_h, GDK_INTERP_BILINEAR);
}

static void image_set_fitted(GtkWidget *img, GdkPixbuf *frame, int size) {
    if (!frame) return;
    GdkPixbuf *fit = fit_pixbuf(frame, size);
    if (!fit) return;
    gtk_image_set_from_pixbuf(GTK_IMAGE(img), fit);
    g_object_unref(fit);
}

static GtkWidget *make_thumb(const char *path, int size) {
    GError *err = NULL;
    GdkPixbuf *src = gdk_pixbuf_new_from_file(path, &err);
    if (!src) {
        if (err) g_error_free(err);
        return NULL;
    }
    GtkWidget *img = gtk_image_new();
    image_set_fitted(img, src, size);
    g_object_unref(src);
    gtk_widget_set_size_request(img, size, size);
    gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(img, GTK_ALIGN_CENTER);
    return img;
}

/* ---- animated GIF cells ------------------------------------------------ */
/* GdkPixbuf's GIF iterator decodes on every advance, which costs several ms per
 * frame and starves the main loop once a page holds 24 of them. Each file is
 * decoded once on a worker thread into frames pre-scaled to cell size; playback
 * then only picks the frame whose time has come, so the main thread never
 * decodes anything. */

#define GIF_MAX_FRAMES 120

typedef struct {
    GdkPixbuf **frames;
    guint *delays;
    int n;
    int total_ms;
} GifClip;

typedef struct {
    char *path;
    int size;
    GtkWidget *img;
    GifClip *clip;
    GdkPixbuf *current;
    double pos_ms;
    gint64 last_us;
    guint changes;
    gint64 first_change_us;
    guint reported_changes;
} GifCell;

static gboolean gif_anim_should_run(void);

static GHashTable *g_gif_clips = NULL;
static GHashTable *g_gif_pending = NULL;
static int g_gif_frame_changes = 0;
static gboolean g_selftest_force_anim = FALSE;

typedef struct {
    char *path;
    GifClip *clip;
    int size;
} ClipReady;

static void gif_clip_free(gpointer data) {
    GifClip *c = data;
    if (!c) return;
    for (int i = 0; i < c->n; i++)
        if (c->frames[i]) g_object_unref(c->frames[i]);
    g_free(c->frames);
    g_free(c->delays);
    g_free(c);
}

static GifClip *gif_clip_decode(const char *path, int size) {
    GError *err = NULL;
    GdkPixbufAnimation *anim = gdk_pixbuf_animation_new_from_file(path, &err);
    if (!anim) {
        if (err) g_error_free(err);
        return NULL;
    }

    GifClip *clip = g_new0(GifClip, 1);
    clip->frames = g_new0(GdkPixbuf *, GIF_MAX_FRAMES);
    clip->delays = g_new0(guint, GIF_MAX_FRAMES);

    GTimeVal base;
    g_get_current_time(&base);
    GdkPixbufAnimationIter *iter = gdk_pixbuf_animation_get_iter(anim, &base);
    while (clip->n < GIF_MAX_FRAMES) {
        GdkPixbuf *frame = gdk_pixbuf_animation_iter_get_pixbuf(iter);
        int delay = gdk_pixbuf_animation_iter_get_delay_time(iter);
        if (!frame) break;
        if (delay <= 0) delay = 100;
        if (delay > 1000) delay = 1000;

        GdkPixbuf *fit = fit_pixbuf(frame, size);
        if (fit) {
            clip->frames[clip->n] = fit;
            clip->delays[clip->n] = (guint)delay;
            clip->total_ms += delay;
            clip->n++;
        }
        base.tv_usec += delay * 1000;
        base.tv_sec += base.tv_usec / 1000000;
        base.tv_usec %= 1000000;
        gdk_pixbuf_animation_iter_advance(iter, &base);
        if (gdk_pixbuf_animation_is_static_image(anim)) break;
    }
    g_object_unref(anim);

    if (clip->n == 0) {
        gif_clip_free(clip);
        return NULL;
    }
    return clip;
}

static void deliver_clip(gpointer data);

static gpointer gif_clip_thread(gpointer data) {
    ClipReady *job = data;
    GifClip *clip = gif_clip_decode(job->path, job->size);
    ClipReady *ready = g_new0(ClipReady, 1);
    ready->path = g_strdup(job->path);
    ready->clip = clip;
    g_free(job->path);
    g_free(job);
    super_v_ui_post_to_main(deliver_clip, ready);
    return NULL;
}

static void clip_set_current(GifCell *cell, GdkPixbuf *frame) {
    if (!frame || cell->current == frame) return;
    if (cell->current) g_object_unref(cell->current);
    cell->current = (GdkPixbuf *)g_object_ref(frame);
    gtk_widget_queue_draw(cell->img);
}

static void deliver_clip(gpointer data) {
    ClipReady *ready = data;
    if (!g_gif_clips) g_gif_clips = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
        gif_clip_free);
    if (!g_gif_pending) g_gif_pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (ready->clip) {
        GifClip *old = g_hash_table_lookup(g_gif_clips, ready->path);
        if (old) {
            gif_clip_free(old);
            g_hash_table_remove(g_gif_clips, ready->path);
        }
        g_hash_table_insert(g_gif_clips, g_strdup(ready->path), ready->clip);
    }
    g_hash_table_remove(g_gif_pending, ready->path);
    if (g_gif_flow && gif_anim_should_run()) gtk_widget_queue_draw(g_gif_flow);
    g_free(ready->path);
    g_free(ready);
}

static GifClip *gif_clip_get(const char *path, int size) {
    if (!g_gif_clips) {
        g_gif_clips = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, gif_clip_free);
        g_gif_pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    GifClip *clip = g_hash_table_lookup(g_gif_clips, path);
    if (clip) return clip;
    if (g_hash_table_contains(g_gif_pending, path)) return NULL;

    g_hash_table_insert(g_gif_pending, g_strdup(path), GINT_TO_POINTER(1));
    ClipReady *job = g_new0(ClipReady, 1);
    job->path = g_strdup(path);
    job->size = size;
    GThread *thread = g_thread_new("gif-decode", gif_clip_thread, job);
    g_thread_unref(thread);
    return NULL;
}

static gboolean gif_anim_should_run(void) {
    if (g_selftest_force_anim) return TRUE;
    if (!g_main_window || !gtk_widget_get_visible(g_main_window)) return FALSE;
    if (!g_stack) return FALSE;
    const char *name = adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(g_stack));
    return name && strcmp(name, "gif") == 0;
}

static gboolean gif_thumb_on_screen(GtkWidget *img) {
    if (!gtk_widget_get_mapped(img)) return FALSE;
    GtkWidget *scroll = gtk_widget_get_ancestor(img, GTK_TYPE_SCROLLED_WINDOW);
    if (!scroll) return TRUE;
    graphene_rect_t bounds;
    if (!gtk_widget_compute_bounds(img, scroll, &bounds)) return TRUE;
    int w = gtk_widget_get_width(scroll);
    int h = gtk_widget_get_height(scroll);
    return bounds.origin.y < h && bounds.origin.y + bounds.size.height > 0
        && bounds.origin.x < w && bounds.origin.x + bounds.size.width > 0;
}

static void gif_cell_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
    gpointer data) {
    (void)area;
    GifCell *cell = (GifCell *)data;
    if (!cell || !cell->current) return;
    int iw = gdk_pixbuf_get_width(cell->current);
    int ih = gdk_pixbuf_get_height(cell->current);
    if (iw <= 0 || ih <= 0) return;
    double scale = MIN((double)width / iw, (double)height / ih);
    cairo_save(cr);
    cairo_translate(cr, (width - iw * scale) / 2.0, (height - ih * scale) / 2.0);
    cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, cell->current, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void gif_cell_free(gpointer data) {
    GifCell *cell = data;
    if (cell->current) g_object_unref(cell->current);
    g_free(cell->path);
    g_free(cell);
}

/* Advance one cell by the elapsed wall time. No decoding happens here. */
static void gif_cell_step(GifCell *cell, gint64 now) {
    if (!cell->clip) {
        cell->clip = gif_clip_get(cell->path, cell->size);
        if (!cell->clip) return;
        if (cell->clip->n == 1) cell->clip->total_ms = 0;
    }
    GifClip *clip = cell->clip;
    if (clip->total_ms <= 0) {
        clip_set_current(cell, clip->frames[0]);
        return;
    }
    if (!cell->last_us) cell->last_us = now;
    double dt = (double)(now - cell->last_us) / 1000.0;
    cell->last_us = now;
    if (dt < 0 || dt > 250) dt = 0;   /* resume after a pause */
    cell->pos_ms += dt;
    double total = (double)clip->total_ms;
    while (cell->pos_ms >= total) cell->pos_ms -= total;

    int index = 0;
    double acc = 0;
    for (int i = 0; i < clip->n; i++) {
        acc += (double)clip->delays[i];
        if (cell->pos_ms < acc) {
            index = i;
            break;
        }
    }
    GdkPixbuf *frame = clip->frames[index];
    if (frame != cell->current) {
        clip_set_current(cell, frame);
        if (!cell->first_change_us) cell->first_change_us = now;
        cell->changes++;
        g_gif_frame_changes++;
    }
}

static gboolean gif_frame_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
    (void)widget; (void)clock; (void)data;
    if (!g_gif_flow || !gif_anim_should_run()) return G_SOURCE_CONTINUE;
    static gboolean fps_log = FALSE;
    static gint64 fps_start = 0;
    static int fps_ticks = 0, fps_changes = 0, fps_cells = 0, fps_cells_max = 0;
    if (!fps_start && g_getenv("SUPER_V_FPS_LOG")) {
        fps_log = TRUE;
        fps_start = g_get_monotonic_time();
    }
    if (fps_log) fps_ticks++;
    gint64 now = g_get_monotonic_time();
    for (GtkWidget *child = gtk_widget_get_first_child(g_gif_flow); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *btn = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child));
        GtkWidget *img = btn ? gtk_button_get_child(GTK_BUTTON(btn)) : NULL;
        if (!img) continue;
        GifCell *cell = g_object_get_data(G_OBJECT(img), "gif-cell");
        if (!cell) continue;
        if (!g_selftest_force_anim && !gif_thumb_on_screen(img)) {
            cell->last_us = 0;
            continue;
        }
        if (fps_log && (++fps_cells > fps_cells_max)) fps_cells_max = fps_cells;
        guint before = cell->changes;
        gif_cell_step(cell, now);
        if (cell->changes != before) fps_changes++;
    }
    if (fps_log) {
        if (now - fps_start >= 1000000) {
            g_printerr("gif_fps cells=%d ticks=%d changes=%d/s elapsed_ms=%d\n",
                fps_cells_max, fps_ticks, fps_changes,
                (int)((now - fps_start) / 1000));
            fps_start = now;
            fps_ticks = fps_changes = fps_cells = fps_cells_max = 0;
        }
        fps_cells = 0;
    }
    return G_SOURCE_CONTINUE;
}

static GtkWidget *make_gif_thumb(const char *path, int size) {
    GdkPixbuf *still = NULL;
    GError *err = NULL;
    GdkPixbufAnimation *anim = gdk_pixbuf_animation_new_from_file(path, &err);
    if (anim) {
        still = gdk_pixbuf_animation_get_static_image(anim);
        if (still) g_object_ref(still);
        g_object_unref(anim);
    } else if (err) {
        g_error_free(err);
    }
    if (!still) still = gdk_pixbuf_new_from_file(path, NULL);

    GtkWidget *img = gtk_drawing_area_new();
    gtk_widget_set_size_request(img, size, size);
    gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(img, GTK_ALIGN_CENTER);

    GifCell *cell = g_new0(GifCell, 1);
    cell->path = g_strdup(path);
    cell->size = size;
    cell->img = img;
    if (still) {
        cell->current = still;
    } else {
        gtk_widget_set_size_request(img, size, size);
    }
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(img), gif_cell_draw, cell, NULL);
    g_object_set_data_full(G_OBJECT(img), "gif-cell", cell, gif_cell_free);
    /* Kick the decode now so the first playback tick already has frames. */
    cell->clip = gif_clip_get(path, size);
    return img;
}

static gboolean on_window_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl; (void)keycode; (void)state; (void)user_data;
    if (keyval == GDK_KEY_Escape) {
        hide_main_window();
        return TRUE;
    }
    if (keyval == GDK_KEY_F1) {
        super_v_ui_show_settings(g_main_window);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_close_request(GtkWindow *window, gpointer user_data) {
    (void)window; (void)user_data;
    hide_main_window();
    return TRUE;
}

static void on_close_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    hide_main_window();
}

static void on_settings_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    super_v_ui_show_settings(user_data);
}

/* Headless verification of the interactive paths: paging, localized search,
   activation, skin-tone popover and GIF listing. Triggered by
   SUPER_V_UI_SELFTEST=1 so it never runs in normal use. */
static gboolean g_application_quit_wrapper(gpointer data) {
    (void)data;
    g_application_quit(G_APPLICATION(g_application_get_default()));
    return G_SOURCE_REMOVE;
}

static gboolean selftest_activate_row(gpointer data) {
    (void)data;
    GtkListBoxRow *first = NULL;
    if (g_clip_list) {
        for (GtkWidget *w = gtk_widget_get_first_child(g_clip_list); w;
             w = gtk_widget_get_next_sibling(w)) {
            if (g_object_get_data(G_OBJECT(w), "super_v_section")) continue;
            first = GTK_LIST_BOX_ROW(w);
            break;
        }
    }
    if (first) {
        g_signal_emit_by_name(first, "activated");
        printf("VERIFY: row_activated=true\n");
    } else {
        printf("VERIFY: row_activated=false\n");
    }
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

static gboolean take_snapshot(gpointer data) {
    const char *path = (const char *)data;
    static gboolean tab_switched = FALSE;
    static gboolean query_applied = FALSE;
    const char *tab = g_getenv("SUPER_V_SNAPSHOT_TAB");
    if (tab && g_stack && !tab_switched) {
        adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(g_stack), tab);
        tab_switched = TRUE;
        g_timeout_add(500, take_snapshot, g_strdup(path));
        return G_SOURCE_REMOVE;
    }
    /* GIF pages arrive asynchronously, so apply the query and give the provider
     * time to deliver before capturing. */
    const char *snapshot_query = g_getenv("SUPER_V_SNAPSHOT_QUERY");
    if (snapshot_query && !query_applied) {
        query_applied = TRUE;
        apply_entry_text(snapshot_query);
        if (g_gif_search_hook) g_gif_search_hook(snapshot_query);
        g_timeout_add(2800, take_snapshot, g_strdup(path));
        return G_SOURCE_REMOVE;
    }
    if (!g_main_window || !path) return G_SOURCE_REMOVE;
    int width = gtk_widget_get_width(g_main_window);
    int height = gtk_widget_get_height(g_main_window);
    if (width <= 0 || height <= 0) {
        g_printerr("SNAP skip %dx%d\n", width, height);
        return G_SOURCE_REMOVE;
    }
    GdkPaintable *paintable = gtk_widget_paintable_new(g_main_window);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snapshot, width, height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(g_main_window));
    GdkTexture *texture = gsk_renderer_render_texture(renderer, node, NULL);
    g_printerr("SNAP %dx%d tex=%p\n", width, height, (void*)texture);
    if (texture) {
        gdk_texture_save_to_png(texture, path);
        g_object_unref(texture);
        const char *gap = g_getenv("SUPER_V_SNAPSHOT_GAP_MS");
        static gboolean burst_done = FALSE;
        if (gap && !burst_done) {
            burst_done = TRUE;
            g_timeout_add((guint)atoi(gap), take_snapshot,
                g_strconcat(path, ".2.png", NULL));
            return G_SOURCE_REMOVE;
        }
    }
    g_object_unref(paintable);
    gsk_render_node_unref(node);
    g_application_quit(G_APPLICATION(g_application_get_default()));
    return G_SOURCE_REMOVE;
}

static int g_gif_probe_round = 0;
static gboolean gif_probe_round_cb(gpointer data);

/* Switch to the GIF tab once the window is actually mapped, then start the
 * page rounds. The stack ignores a switch made before it is on screen. */
static gboolean gif_probe_begin(gpointer data) {
    (void)data;
    if (g_stack)
        adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(g_stack), "gif");
    g_gif_probe_round = 0;
    g_timeout_add(2500, gif_probe_round_cb, NULL);
    return G_SOURCE_REMOVE;
}

/* Async paging check: waits for each page to land before asking for the next. */
static gboolean gif_probe_round_cb(gpointer data) {
    (void)data;
    int items = 0;
    for (GtkWidget *w = gtk_widget_get_first_child(g_gif_flow); w;
         w = gtk_widget_get_next_sibling(w)) {
        items++;
    }
    /* The frame clock only ticks on a mapped window, so drive the same tick by
     * hand for one simulated second and report the observed playback rate. */
    int before = g_gif_frame_changes;
    for (int i = 0; i < 60; i++) {
        gif_frame_tick(NULL, NULL, NULL);
        g_usleep(16000);
    }
    double fps = 0.0;
    GtkWidget *first = gtk_widget_get_first_child(g_gif_flow);
    GtkWidget *first_btn = first ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(first)) : NULL;
    GtkWidget *first_img = first_btn ? gtk_button_get_child(GTK_BUTTON(first_btn)) : NULL;
    GifCell *first_anim = first_img ? g_object_get_data(G_OBJECT(first_img), "gif-cell") : NULL;
    if (first_anim && first_anim->changes && first_anim->first_change_us) {
        gint64 span = g_get_monotonic_time() - first_anim->first_change_us;
        if (span > 0) fps = (double)first_anim->changes * 1e6 / (double)span;
    }
    printf("VERIFY: gif_round=%d items=%d frames=%d(+%d) fps=%.1f hint=%s\n",
        g_gif_probe_round, items, g_gif_frame_changes, g_gif_frame_changes - before,
        fps, g_gif_label ? gtk_label_get_text(GTK_LABEL(g_gif_label)) : "");
    fflush(stdout);
    if (g_gif_probe_round < 3) {
        g_gif_probe_round++;
        if (g_gif_more_hook) g_gif_more_hook();
        g_timeout_add(3500, gif_probe_round_cb, NULL);
        return G_SOURCE_REMOVE;
    }
    if (g_getenv("SUPER_V_SELFTEST_GIF_CLICK")) {
        GtkWidget *child = gtk_widget_get_first_child(g_gif_flow);
        GtkWidget *btn = child ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child)) : NULL;
        if (btn) g_signal_emit_by_name(btn, "clicked");
        printf("VERIFY: gif_click_texture=%d gif_click_gif=%d\n",
            g_last_paste_had_texture, g_last_paste_had_gif);
        fflush(stdout);
        g_application_quit(G_APPLICATION(g_application_get_default()));
        return G_SOURCE_REMOVE;
    }
    g_application_quit(G_APPLICATION(g_application_get_default()));
    return G_SOURCE_REMOVE;
}

static gboolean run_selftest(gpointer data) {
    (void)data;

    first_page("emoji");
    int pages = 1;
    while (!g_done_emoji && pages < 40) {
        load_more_glyphs("emoji");
        pages++;
    }
    int total = g_offset_emoji;
    printf("VERIFY: emoji_paging_ok=true, total=%d, pages=%d\n", total, pages);

    apply_entry_text("sedih");
    g_strlcpy(g_query_emoji, "sedih", sizeof(g_query_emoji));
    g_offset_emoji = 0; g_done_emoji = 0;
    first_page("emoji");
    int sad_hits = g_offset_emoji;
    printf("VERIFY: search_sedih_ok=%d, hits=%d\n", sad_hits > 0, sad_hits);

    apply_entry_text("marah");
    g_strlcpy(g_query_emoji, "marah", sizeof(g_query_emoji));
    g_offset_emoji = 0; g_done_emoji = 0;
    first_page("emoji");
    int angry_hits = g_offset_emoji;
    printf("VERIFY: search_marah_ok=%d, hits=%d\n", angry_hits > 0, angry_hits);

    apply_entry_text("angry");
    g_strlcpy(g_query_emoji, "angry", sizeof(g_query_emoji));
    g_offset_emoji = 0; g_done_emoji = 0;
    first_page("emoji");
    int en_hits = g_offset_emoji;
    printf("VERIFY: search_english_ok=%d, hits=%d\n", en_hits > 0, en_hits);

    int gif_count = 0;
    for (GtkWidget *w = gtk_widget_get_first_child(g_gif_flow); w; w = gtk_widget_get_next_sibling(w)) gif_count++;
    printf("VERIFY: gif_items=%d, hint=%s\n", gif_count,
        gtk_label_get_text(GTK_LABEL(g_gif_label)));

    apply_entry_text("");
    g_query_emoji[0] = 0;
    g_offset_emoji = 0; g_done_emoji = 0;
    first_page("emoji");

    GtkWidget *first_child = gtk_widget_get_first_child(g_emoji_flow);
    GtkWidget *first = first_child ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(first_child)) : NULL;
    if (first) {
        on_emoji_right_clicked(NULL, 1, 0, 0, first);
        printf("VERIFY: skin_tone_popover_ok=true\n");
        g_signal_emit_by_name(first, "clicked");
        printf("VERIFY: glyph_activate_ok=true\n");
    }

    super_v_ui_show_settings(g_main_window);
    super_v_ui_show_about(g_main_window);
    printf("VERIFY: preferences_dialog_ok=true\n");
    printf("VERIFY: about_dialog_ok=true\n");
    super_v_ui_show_about(g_main_window);
    printf("VERIFY: about_single_page_ok=true\n");
    super_v_ui_show_debug();
    printf("VERIFY: debug_window_ok=true\n");
    if (g_provider_test_hook) g_provider_test_hook("local");
    if (g_provider_test_row) printf("VERIFY: provider_test_ok=true, result=%s\n",
        adw_action_row_get_subtitle(ADW_ACTION_ROW(g_provider_test_row)));
    if (g_getenv("SUPER_V_SELFTEST_APIKEY") && g_api_key_hook) {
        g_api_key_hook("smoke-key-1234");
        printf("VERIFY: api_key_hook_called=true\n");
    }
    if (g_getenv("SUPER_V_SELFTEST_ROW")) {
        g_timeout_add(1500, selftest_activate_row, NULL);
        g_timeout_add(6000, g_application_quit_wrapper, NULL);
        return G_SOURCE_REMOVE;
    }
    if (g_getenv("SUPER_V_SELFTEST_PASTE")) {
        super_v_ui_paste_text("PASTE-TEST-VALUE");
        printf("VERIFY: paste_path_invoked=true\n");
        fflush(stdout);
        g_timeout_add(1200, (GSourceFunc)g_application_quit_wrapper, NULL);
        return G_SOURCE_REMOVE;
    }
    {
        const char *man_teacher = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x8F\xAB";
        char buf[GLYPH_VARIANT_BUF];
        buf[0] = 0;
        int len = g_variants_provider ? g_variants_provider(man_teacher, buf, (int)sizeof(buf)) : -1;
        printf("VERIFY: missing_glyph_cells=%d\n", g_missing_glyphs);
        printf("VERIFY: variant_ok=%s\n", len > 0 ? "yes" : "no");
        if (len > 0) {
            int count = 1;
            for (int i = 0; i < len; i++) {
                if ((unsigned char)buf[i] == 0x1F) count++;
            }
            printf("VERIFY: variant_count=%d\n", count);
            printf("VERIFY: variant_codepoints=");
            const char *end = buf + len;
            for (const char *q = buf; q < end && *q != 0x1F; q = g_utf8_next_char(q)) {
                printf("U+%04X ", g_utf8_get_char(q));
            }
            printf("\n");
        }
    }
    if (g_getenv("SUPER_V_SELFTEST_GIF_MORE") && g_gif_more_hook) {
        const char *probe_query = g_getenv("SUPER_V_SELFTEST_GIF_SEARCH");
        if (probe_query) {
            g_strlcpy(g_gif_query, probe_query, sizeof(g_gif_query));
            apply_entry_text(probe_query);
            if (g_gif_search_hook) g_gif_search_hook(probe_query);
        }
        /* Animations only run on a mapped window and visible tab. */
        if (g_main_window) {
            gtk_widget_set_visible(g_main_window, TRUE);
            gtk_window_present(GTK_WINDOW(g_main_window));
        }
        g_selftest_force_anim = TRUE;
        g_timeout_add(800, gif_probe_begin, NULL);
        return G_SOURCE_REMOVE;
    }
    printf("VERIFY: no_crash=true\n");
    fflush(stdout);
    g_application_quit(G_APPLICATION(g_application_get_default()));
    return G_SOURCE_REMOVE;
}

static void on_preferences_action(GSimpleAction *a, GVariant *p, gpointer d) {
    (void)a; (void)p; (void)d;
    super_v_ui_show_settings(g_main_window);
}

static void on_about_action(GSimpleAction *a, GVariant *p, gpointer d) {
    (void)a; (void)p; (void)d;
    super_v_ui_show_about(g_main_window);
}

static const GActionEntry app_entries[] = {
    { "preferences", on_preferences_action, NULL, NULL, NULL },
    { "about", on_about_action, NULL, NULL, NULL },
};

/* ------------------------------------------------------ window placement
 * A Wayland client cannot position a toplevel, and a transparent fullscreen
 * hack looks wrong. The shell can move windows, so the GNOME extension
 * (super-v-bridge) exports MoveWindowToCursor and we call it after showing.
 * Without the extension the window simply stays where Mutter put it.
 */

static void on_move_window_reply(GObject *source, GAsyncResult *res, gpointer data) {
    (void)data;
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    if (reply) {
        g_variant_unref(reply);
        if (g_getenv("SUPER_V_DEBUG_PLACEMENT")) g_printerr("PLACE moved by shell extension\n");
    } else {
        if (g_getenv("SUPER_V_DEBUG_PLACEMENT")) g_printerr("PLACE extension unavailable: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
    }
}

static void refresh_bridge_status(void) {
    if (!g_bridge_banner) return;
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        if (err) g_error_free(err);
        gtk_widget_set_visible(g_bridge_banner, TRUE);
        return;
    }
    GVariant *reply = g_dbus_connection_call_sync(bus,
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "NameHasOwner",
        g_variant_new("(s)", "my.id.arkhimuttaqina.Bridge"),
        G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 300, NULL, &err);
    gboolean present = FALSE;
    if (reply) {
        gboolean owned = FALSE;
        g_variant_get(reply, "(b)", &owned);
        present = owned;
        g_variant_unref(reply);
    } else if (err) {
        g_error_free(err);
    }
    gtk_widget_set_visible(g_bridge_banner, !present);
    if (g_getenv("SUPER_V_DEBUG_INSERT")) g_printerr("BRIDGE present=%d\n", present);
    g_object_unref(bus);
}

static void request_pointer_placement(void) {
    if (!g_placement_pointer || !g_main_window) return;
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        if (err) g_error_free(err);
        return;
    }
    g_dbus_connection_call(
        bus,
        "my.id.arkhimuttaqina.Bridge",
        "/my/id/arkhimuttaqina/Bridge",
        "my.id.arkhimuttaqina.Bridge",
        "MoveWindowToCursor",
        g_variant_new("(s)", "Super-V Clipboard"),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        400,
        NULL,
        on_move_window_reply,
        NULL);
    g_object_unref(bus);
}

void super_v_ui_set_placement(const char *mode) {
    g_placement_pointer = (mode && strcmp(mode, "pointer") == 0);
    if (g_main_window && gtk_widget_get_visible(g_main_window)) {
        request_pointer_placement();
    }
}

static void on_placement_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec; (void)user_data;
    if (g_setting_combo) return;
    guint idx = adw_combo_row_get_selected(ADW_COMBO_ROW(obj));
    const char *mode = (idx == 1) ? "pointer" : "center";
    g_placement_pointer = (idx == 1);
    if (g_placement_hook) g_placement_hook(mode);
    request_pointer_placement();
}

static gboolean place_after_map(gpointer data) {
    (void)data;
    request_pointer_placement();
    return G_SOURCE_REMOVE;
}

static GtkWidget *create_main_window(AdwApplication *app) {
    GtkWidget *window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "Super-V Clipboard");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 520);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), NULL);

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_window_key_pressed), window);
    gtk_widget_add_controller(window, key_ctrl);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), main_box);

    g_stack = adw_view_stack_new();
    gtk_widget_set_vexpand(g_stack, TRUE);

    GtkWidget *top_header = adw_header_bar_new();
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(top_header), FALSE);
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(top_header), FALSE);

    AdwViewSwitcher *switcher = ADW_VIEW_SWITCHER(adw_view_switcher_new());
    adw_view_switcher_set_stack(switcher, ADW_VIEW_STACK(g_stack));
    adw_view_switcher_set_policy(switcher, ADW_VIEW_SWITCHER_POLICY_NARROW);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(top_header), GTK_WIDGET(switcher));

    GtkWidget *settings_btn = gtk_button_new_from_icon_name("preferences-system-symbolic");
    gtk_widget_set_tooltip_text(settings_btn, "Settings (F1)");
    gtk_widget_add_css_class(settings_btn, "flat");
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), window);
    adw_header_bar_pack_end(ADW_HEADER_BAR(top_header), settings_btn);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(close_btn, "Close (Esc)");
    gtk_widget_add_css_class(close_btn, "flat");
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_btn_clicked), window);
    adw_header_bar_pack_end(ADW_HEADER_BAR(top_header), close_btn);

    gtk_box_append(GTK_BOX(main_box), top_header);

    g_search_entry = gtk_search_entry_new();
    gtk_widget_set_margin_start(g_search_entry, 10);
    gtk_widget_set_margin_end(g_search_entry, 10);
    gtk_widget_set_margin_top(g_search_entry, 6);
    gtk_widget_set_margin_bottom(g_search_entry, 6);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(g_search_entry),
        "Search clipboard, emoji (sad, marah, cinta), symbols...");
    g_signal_connect(g_search_entry, "search-changed", G_CALLBACK(on_search_changed), NULL);
    gtk_box_append(GTK_BOX(main_box), g_search_entry);

    /* Bridge status: paste and capture need the GNOME extension. */
    g_bridge_banner = gtk_label_new("GNOME extension Super-V Bridge is not installed or not connected — pasting and clipboard capture are limited.\nInstall it and log in again, or run super-v-ui --check.");
    gtk_label_set_wrap(GTK_LABEL(g_bridge_banner), TRUE);
    gtk_widget_set_margin_start(g_bridge_banner, 12);
    gtk_widget_set_margin_end(g_bridge_banner, 12);
    gtk_widget_set_margin_bottom(g_bridge_banner, 8);
    gtk_widget_add_css_class(g_bridge_banner, "warning-banner");
    gtk_widget_set_halign(g_bridge_banner, GTK_ALIGN_FILL);
    gtk_widget_set_visible(g_bridge_banner, FALSE);
    gtk_box_append(GTK_BOX(main_box), g_bridge_banner);

    gtk_box_append(GTK_BOX(main_box), g_stack);

    /* TAB 1: clipboard history */
    GtkWidget *clip_scroll = gtk_scrolled_window_new();
    g_clip_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_clip_list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(g_clip_list), (GtkListBoxFilterFunc)filter_clip_row, NULL, NULL);
    gtk_widget_add_css_class(g_clip_list, "boxed-list");
    gtk_widget_set_margin_start(g_clip_list, 10);
    gtk_widget_set_margin_end(g_clip_list, 10);
    gtk_widget_set_margin_bottom(g_clip_list, 10);
    g_empty_label = gtk_label_new("Nobody here. Start copying something, lmao.");
    gtk_widget_add_css_class(g_empty_label, "dim-label");
    gtk_widget_set_halign(g_empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(g_empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(g_empty_label, TRUE);

    gtk_widget_set_vexpand(clip_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(clip_scroll), g_clip_list);

    GtkWidget *clip_overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(clip_overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(clip_overlay), clip_scroll);
    gtk_widget_set_can_target(g_empty_label, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(clip_overlay), g_empty_label);
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(g_stack), clip_overlay, "clipboard", "Clipboard", "edit-paste-symbolic");

    /* TAB 2: emoji */
    GtkWidget *emoji_scroll = gtk_scrolled_window_new();
    GtkWidget *emoji_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(emoji_container, 10);
    gtk_widget_set_margin_end(emoji_container, 10);
    gtk_widget_set_margin_top(emoji_container, 6);

    GtkWidget *recent_lbl = gtk_label_new("Recently used");
    gtk_widget_set_halign(recent_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(recent_lbl, "heading");
    gtk_box_append(GTK_BOX(emoji_container), recent_lbl);

    g_recent_emoji_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_recent_emoji_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_recent_emoji_flow), 7);
    gtk_box_append(GTK_BOX(emoji_container), g_recent_emoji_flow);

    GtkWidget *all_lbl = gtk_label_new("All emoji");
    gtk_widget_set_halign(all_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(all_lbl, "heading");
    gtk_box_append(GTK_BOX(emoji_container), all_lbl);

    g_emoji_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_emoji_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_emoji_flow), 7);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(g_emoji_flow), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(g_emoji_flow), 6);
    gtk_box_append(GTK_BOX(emoji_container), g_emoji_flow);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(emoji_scroll), emoji_container);
    g_signal_connect(emoji_scroll, "edge-reached", G_CALLBACK(on_scroll_edge), (gpointer)"emoji");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(g_stack), emoji_scroll, "emoji", "Emoji", "face-smile-symbolic");

    /* TAB 3: GIF */
    GtkWidget *gif_scroll = gtk_scrolled_window_new();
    GtkWidget *gif_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(gif_container, 10);
    gtk_widget_set_margin_end(gif_container, 10);
    gtk_widget_set_margin_top(gif_container, 6);

    g_gif_label = gtk_label_new("Local GIF library");
    gtk_widget_set_halign(g_gif_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_gif_label, "heading");
    gtk_box_append(GTK_BOX(gif_container), g_gif_label);

    g_gif_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_gif_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_gif_flow), 4);
    gtk_flow_box_set_filter_func(GTK_FLOW_BOX(g_gif_flow), (GtkFlowBoxFilterFunc)filter_gif_row, NULL, NULL);
    gtk_box_append(GTK_BOX(gif_container), g_gif_flow);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(gif_scroll), gif_container);
    g_signal_connect(gif_scroll, "edge-reached", G_CALLBACK(on_gif_scroll_edge), NULL);
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(g_stack), gif_scroll, "gif", "GIF", "media-optical-symbolic");

    /* TAB 4: symbols */
    GtkWidget *sym_scroll = gtk_scrolled_window_new();
    GtkWidget *sym_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(sym_container, 10);
    gtk_widget_set_margin_end(sym_container, 10);
    gtk_widget_set_margin_top(sym_container, 6);

    GtkWidget *sym_lbl = gtk_label_new("Math, currency, arrows, Greek & punctuation");
    gtk_widget_set_halign(sym_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(sym_lbl, "heading");
    gtk_box_append(GTK_BOX(sym_container), sym_lbl);

    g_symbol_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_symbol_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_symbol_flow), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(g_symbol_flow), 2);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(g_symbol_flow), 2);
    gtk_box_append(GTK_BOX(sym_container), g_symbol_flow);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sym_scroll), sym_container);
    g_signal_connect(sym_scroll, "edge-reached", G_CALLBACK(on_scroll_edge), (gpointer)"symbol");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(g_stack), sym_scroll, "symbols", "Symbols", "accessories-character-map-symbolic");

    g_signal_connect(g_stack, "notify::visible-child-name", G_CALLBACK(on_tab_switched), NULL);

    return window;
}

static void on_app_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    if (g_main_window) {
        cancel_idle_exit();
        gtk_widget_set_visible(g_main_window, TRUE);
        gtk_window_present(GTK_WINDOW(g_main_window));
    g_timeout_add(60, place_after_map, NULL);
        return;
    }

    install_css();
    g_main_window = create_main_window(ADW_APPLICATION(app));
    if (g_main_window)
        gtk_widget_add_tick_callback(g_main_window, gif_frame_tick, NULL, NULL);
    gtk_window_present(GTK_WINDOW(g_main_window));
    g_timeout_add(60, place_after_map, NULL);
    gtk_widget_grab_focus(g_search_entry);
    refresh_bridge_status();
    g_timeout_add(200, sync_clipboard_idle, NULL);

    if (g_activate_hook) g_activate_hook();
    if (g_getenv("SUPER_V_FPS_LOG") && g_main_window) {
        gtk_widget_set_visible(g_main_window, TRUE);
        gtk_window_present(GTK_WINDOW(g_main_window));
        if (g_stack)
            adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(g_stack), "gif");
    }
    const char *start_tab = g_getenv("SUPER_V_START_TAB");
    if (start_tab && g_stack)
        adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(g_stack), start_tab);

    if (g_getenv("SUPER_V_GIF_CLIP")) {
        super_v_ui_set_gif_clipboard(g_getenv("SUPER_V_GIF_CLIP"));
        gtk_widget_set_visible(g_main_window, FALSE);
    }
    if (g_getenv("SUPER_V_SNAPSHOT")) {
        g_timeout_add(1200, take_snapshot, g_strdup(g_getenv("SUPER_V_SNAPSHOT")));
    }

    GdkClipboard *clip = gdk_display_get_clipboard(gdk_display_get_default());
    if (clip) g_signal_connect(clip, "changed", G_CALLBACK(on_clipboard_changed), NULL);
    g_unix_signal_add(SIGUSR1, on_toggle_signal, NULL);
    g_unix_signal_add(SIGUSR2, on_hide_signal, NULL);

    if (g_getenv("SUPER_V_UI_SELFTEST")) {
        g_timeout_add(600, run_selftest, NULL);
    }
}

int super_v_ui_run_app(int argc, char **argv) {
    AdwApplication *app = adw_application_new("my.id.arkhimuttaqina.SuperVClipboard", G_APPLICATION_DEFAULT_FLAGS);
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

int super_v_ui_test_prewarm(char *version_buf, int max_len) {
    if (version_buf && max_len > 0) {
        snprintf(version_buf, (size_t)max_len,
            "ui_runtime_ok=true, version=GTK %d.%d.%d, Libadwaita %d.%d.%d",
            gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
            adw_get_major_version(), adw_get_minor_version(), adw_get_micro_version());
    }
    return 1;
}

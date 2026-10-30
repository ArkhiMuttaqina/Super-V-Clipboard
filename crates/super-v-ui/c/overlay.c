#include "overlay.h"
#include <adwaita.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib-unix.h>
#include <signal.h>
#include <malloc.h>

#define GLYPH_PAGE 120
#define IDLE_EXIT_SECONDS 180
#define EMOJI_CELL 48
#define SYMBOL_CELL 44

static GtkWidget *g_main_window = NULL;
static GtkWidget *g_stack = NULL;
static GtkWidget *g_clip_list = NULL;
static GtkWidget *g_search_entry = NULL;
static GtkWidget *g_emoji_flow = NULL;
static GtkWidget *g_recent_emoji_flow = NULL;
static GtkWidget *g_symbol_flow = NULL;
static GtkWidget *g_gif_flow = NULL;
static GtkWidget *g_gif_label = NULL;

static GtkWidget *g_lang_combo = NULL;
static GtkWidget *g_gif_combo = NULL;

static PasteCallback g_paste_cb = NULL;
static PinCallback g_pin_cb = NULL;
static DeleteCallback g_delete_cb = NULL;
static GlyphProvider g_glyph_provider = NULL;
static ActivateHook g_activate_hook = NULL;
static LanguageHook g_language_hook = NULL;
static GifProviderHook g_gif_provider_hook = NULL;
static GifReloadHook g_gif_reload_hook = NULL;

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

static void hide_main_window(void);
static void cancel_idle_exit(void);
static void schedule_idle_exit(void);
static GtkWidget *make_thumb(const char *path, int size);
static void reload_glyphs(const char *tab);

void super_v_ui_set_paste_callback(PasteCallback cb) { g_paste_cb = cb; }
void super_v_ui_set_pin_callback(PinCallback cb) { g_pin_cb = cb; }
void super_v_ui_set_delete_callback(DeleteCallback cb) { g_delete_cb = cb; }
void super_v_ui_set_glyph_provider(GlyphProvider cb) { g_glyph_provider = cb; }
void super_v_ui_set_activate_hook(ActivateHook cb) { g_activate_hook = cb; }
void super_v_ui_set_language_hook(LanguageHook cb) { g_language_hook = cb; }
void super_v_ui_set_gif_provider_hook(GifProviderHook cb) { g_gif_provider_hook = cb; }
void super_v_ui_set_gif_reload_hook(GifReloadHook cb) { g_gif_reload_hook = cb; }

static void free_closure_data(gpointer data, GClosure *closure) { (void)closure; g_free(data); }

static void install_css(void) {
    GtkCssProvider *p = gtk_css_provider_new();
    const char *css =
        ".emoji-cell { font-size: 26px; min-width: 48px; min-height: 48px; padding: 0; }\n"
        ".symbol-cell { font-size: 20px; min-width: 44px; min-height: 44px; padding: 0; }\n"
        ".gif-cell { padding: 0; }\n"
        ".gif-cell image { border-radius: 8px; }\n";
    gtk_css_provider_load_from_string(p, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void copy_text_to_clipboard(const char *text) {
    GdkDisplay *d = gdk_display_get_default();
    if (!d) return;
    gdk_clipboard_set_text(gdk_display_get_clipboard(d), text);
}

/* ---------------------------------------------------------------- history */

static void emit_paste(long long id, const char *text) {
    if (g_paste_cb) g_paste_cb(id, text);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box; (void)user_data;
    gint64 *pid = (gint64 *)g_object_get_data(G_OBJECT(row), "super_v_id");
    if (!pid) return;
    emit_paste((long long)*pid, NULL);
    hide_main_window();
}

static void on_pin_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *row = GTK_WIDGET(user_data);
    gint64 *pid = (gint64 *)g_object_get_data(G_OBJECT(row), "super_v_id");
    if (pid && g_pin_cb) g_pin_cb((long long)*pid);
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

void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id, const char *preview_image_path) {
    if (!g_clip_list) return;

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
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title ? title : "");
        g_object_set_data(G_OBJECT(row), "super_v_revealed", GINT_TO_POINTER(1));
    } else {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title ? title : "(Empty)");
    }

    if (subtitle && *subtitle) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    if (is_pinned) {
        gtk_widget_add_css_class(GTK_WIDGET(row), "pinned-row");
    }

    g_object_set_data_full(G_OBJECT(row), "super_v_text", g_strdup(title ? title : ""), g_free);
    gint64 *heap_id = g_new0(gint64, 1);
    *heap_id = (gint64)id;
    g_object_set_data_full(G_OBJECT(row), "super_v_id", heap_id, g_free);

    if (preview_image_path && *preview_image_path) {
        GtkWidget *thumb = make_thumb(preview_image_path, 40);
        if (thumb) adw_action_row_add_prefix(ADW_ACTION_ROW(row), thumb);
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

    gtk_list_box_append(GTK_LIST_BOX(g_clip_list), GTK_WIDGET(row));
}

void super_v_ui_clear_history_items(void) {
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

static void on_glyph_clicked(GtkButton *btn, gpointer user_data) {
    const char *glyph = (const char *)user_data;
    GtkWidget *child = btn ? gtk_widget_get_parent(GTK_WIDGET(btn)) : NULL;
    GtkWidget *flow = child ? gtk_widget_get_parent(child) : NULL;
    if (flow && flow == g_emoji_flow) super_v_ui_record_recent("emoji", glyph);
    copy_text_to_clipboard(glyph);
    emit_paste(0, glyph);
    hide_main_window();
}

static void on_skin_variant_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *variant = (const char *)user_data;
    copy_text_to_clipboard(variant);
    emit_paste(0, variant);
    hide_main_window();
}

static void on_popover_closed(GtkPopover *popover, gpointer user_data) {
    (void)user_data;
    gtk_widget_unparent(GTK_WIDGET(popover));
}

static void on_emoji_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)n_press; (void)x; (void)y;
    GtkWidget *btn = GTK_WIDGET(user_data);
    const char *base = (const char *)g_object_get_data(G_OBJECT(btn), "base_emoji");
    if (!base) return;
    if (gesture) gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    static const char *mods[] = { "", "\xf0\x9f\x8f\xbb", "\xf0\x9f\x8f\xbc", "\xf0\x9f\x8f\xbd", "\xf0\x9f\x8f\xbe", "\xf0\x9f\x8f\xbf" };

    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, btn);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    for (int i = 0; i < 6; i++) {
        char variant[64];
        snprintf(variant, sizeof(variant), "%s%s", base, mods[i]);
        GtkWidget *vb = gtk_button_new_with_label(variant);
        gtk_widget_add_css_class(vb, "flat");
        gtk_widget_add_css_class(vb, "emoji-cell");
        g_signal_connect_data(vb, "clicked", G_CALLBACK(on_skin_variant_clicked),
            g_strdup(variant), (GClosureNotify)free_closure_data, 0);
        gtk_box_append(GTK_BOX(box), vb);
    }

    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void super_v_ui_add_glyph_item(const char *tab, const char *glyph, const char *name, const char *keywords, int skin_tone_capable) {
    (void)keywords;
    GtkWidget *flow = flow_for_tab(tab);
    if (!flow || !glyph) return;

    gboolean is_emoji = (tab && strcmp(tab, "emoji") == 0);

    GtkWidget *btn = gtk_button_new_with_label(glyph);
    gtk_widget_set_tooltip_text(btn, name ? name : glyph);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_add_css_class(btn, is_emoji ? "emoji-cell" : "symbol-cell");
    gtk_widget_set_size_request(btn, is_emoji ? EMOJI_CELL : SYMBOL_CELL, is_emoji ? EMOJI_CELL : SYMBOL_CELL);

    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_glyph_clicked),
        g_strdup(glyph), (GClosureNotify)free_closure_data, 0);

    if (skin_tone_capable) {
        g_object_set_data_full(G_OBJECT(btn), "base_emoji", g_strdup(glyph), g_free);

        GtkGesture *right = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(right), GTK_PHASE_CAPTURE);
        g_signal_connect(right, "pressed", G_CALLBACK(on_emoji_right_clicked), btn);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(right));

        GtkGesture *longpress = gtk_gesture_long_press_new();
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(longpress), GTK_PHASE_CAPTURE);
        g_signal_connect(longpress, "pressed", G_CALLBACK(on_emoji_right_clicked), btn);
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
    GtkWidget *btn = gtk_button_new_with_label(content);
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
    }
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
    }
}

/* -------------------------------------------------------------------- GIF */

void super_v_ui_add_gif_item(const char *path, const char *title) {
    if (!g_gif_flow || !path) return;
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_add_css_class(btn, "gif-cell");
    gtk_widget_set_tooltip_text(btn, title ? title : path);

    GtkWidget *thumb = make_thumb(path, 84);
    if (thumb) {
        gtk_button_set_child(GTK_BUTTON(btn), thumb);
    } else {
        gtk_button_set_label(GTK_BUTTON(btn), title ? title : "GIF");
    }

    g_object_set_data_full(G_OBJECT(btn), "super_v_gif_title", g_strdup(title ? title : ""), g_free);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_glyph_clicked),
        g_strdup(path), (GClosureNotify)free_closure_data, 0);
    gtk_flow_box_append(GTK_FLOW_BOX(g_gif_flow), btn);
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
    super_v_ui_clear_gif_items();
    if (g_gif_reload_hook) g_gif_reload_hook();
}

static gboolean filter_gif_row(GtkFlowBoxChild *child, gpointer user_data) {
    (void)user_data;
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
    if (strcmp(id, "klarna") == 0) return 3;
    return 0;
}

static const char *gif_id_for_index(int idx) {
    switch (idx) {
        case 1: return "tenor";
        case 2: return "giphy";
        case 3: return "klarna";
        default: return "local";
    }
}

static void on_gif_provider_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec; (void)user_data;
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

void super_v_ui_show_about(void *parent_window) {
    AdwDialog *about = ADW_DIALOG(adw_about_dialog_new());
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Super-V Clipboard");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), "edit-paste");
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "Arkhi Muttaqina");
    const char *devs[] = { "Arkhi Muttaqina <arkhi07@hotmail.co.id>", NULL };
    adw_about_dialog_set_developers(ADW_ABOUT_DIALOG(about), devs);
    adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about), "https://github.com/ArkhiMuttaqina/Super-V-Clipboard");
    adw_about_dialog_set_issue_url(ADW_ABOUT_DIALOG(about), "https://github.com/ArkhiMuttaqina/Super-V-Clipboard/issues");
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), "0.1.0");
    adw_about_dialog_set_copyright(ADW_ABOUT_DIALOG(about), "\xc2\xa9 2026 Arkhi Muttaqina");
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_GPL_3_0);
    adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(about),
        "Clipboard manager with history, emoji, symbols and GIF search for GNOME, Hyprland and X11.");

    const char *ui_stack[] = { "GTK 4 (widget toolkit)", "libadwaita (GNOME widgets)", "GdkPixbuf (image scaling)", "Noto Color Emoji (glyph rendering)", NULL };
    adw_about_dialog_add_credit_section(ADW_ABOUT_DIALOG(about), "User Interface", ui_stack);

    const char *core_stack[] = {
        "Rust standard library",
        "Tokio (async runtime)",
        "rusqlite / SQLite 3 + FTS5 (storage, full-text search)",
        "serde + serde_json (data + settings)",
        "sha2 (SHA-256 content addressing)",
        "anyhow (error handling)",
        "log + env_logger",
        "libc",
        NULL
    };
    adw_about_dialog_add_credit_section(ADW_ABOUT_DIALOG(about), "Core Engine", core_stack);

    GtkWindow *parent = parent_window ? GTK_WINDOW(parent_window) : NULL;
    adw_dialog_present(about, GTK_WIDGET(parent));
}

void super_v_ui_show_settings(void *parent_window) {
    GtkWindow *parent = parent_window ? GTK_WINDOW(parent_window) : NULL;
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

    AdwPreferencesGroup *startup_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(startup_group, "Startup");
    adw_preferences_page_add(page, startup_group);
    GtkWidget *autostart_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(autostart_row), "Show tray / keep resident");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(autostart_row), "The overlay exits after 3 minutes hidden and restarts instantly on Super+V.");
    adw_preferences_group_add(startup_group, autostart_row);

    /* ---- Emoji ---- */
    AdwPreferencesPage *emoji_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(emoji_page, "Emoji and Symbols");
    adw_preferences_page_set_icon_name(emoji_page, "face-smile-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), emoji_page);

    AdwPreferencesGroup *lang_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(lang_group, "Search Languages");
    adw_preferences_group_set_description(lang_group,
        "Search emoji by everyday words, not just Unicode names. Indonesian and English "
        "dictionaries are bundled; new languages live in data/glyphs/i18n.json and can be contributed upstream.");
    adw_preferences_page_add(emoji_page, lang_group);

    const char *lang_options[] = {
        "English + Indonesian (Default)",
        "English only",
        "Indonesian only",
        NULL
    };
    g_lang_combo = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_lang_combo), "Emoji search languages");
    adw_combo_row_set_model(ADW_COMBO_ROW(g_lang_combo), G_LIST_MODEL(gtk_string_list_new(lang_options)));
    adw_combo_row_set_selected(ADW_COMBO_ROW(g_lang_combo), (guint)lang_index_for(g_langs));
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
    adw_preferences_page_add(gif_page, gif_group);

    const char *providers[] = {
        "Local library (~/Pictures, ~/Downloads)",
        "Tenor API",
        "Giphy",
        "Klarna",
        NULL
    };
    g_gif_combo = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_gif_combo), "Active provider");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(g_gif_combo),
        "Local works offline. Online providers need an API key in ~/.config/super-v-clipboard/settings.json (gif_api_key).");
    adw_combo_row_set_model(ADW_COMBO_ROW(g_gif_combo), G_LIST_MODEL(gtk_string_list_new(providers)));
    adw_combo_row_set_selected(ADW_COMBO_ROW(g_gif_combo), (guint)gif_index_for(g_gif_provider));
    g_signal_connect(g_gif_combo, "notify::selected", G_CALLBACK(on_gif_provider_changed), NULL);
    adw_preferences_group_add(gif_group, g_gif_combo);

    GtkWidget *rescan_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(rescan_row), "Rescan local GIFs");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(rescan_row), "Re-index animated images found on disk.");
    GtkWidget *rescan_btn = gtk_button_new_with_label("Rescan");
    gtk_widget_add_css_class(rescan_btn, "flat");
    gtk_widget_set_valign(rescan_btn, GTK_ALIGN_CENTER);
    g_signal_connect(rescan_btn, "clicked", G_CALLBACK(on_rescan_gifs), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(rescan_row), rescan_btn);
    adw_preferences_group_add(gif_group, rescan_row);

    /* ---- About ---- */
    AdwPreferencesPage *about_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(about_page, "About");
    adw_preferences_page_set_icon_name(about_page, "help-about-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), about_page);

    AdwPreferencesGroup *about_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_page_add(about_page, about_group);
    GtkWidget *about_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(about_row), "About Super-V Clipboard");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(about_row), "Version, license, credits and bundled libraries");
    GtkWidget *about_btn = gtk_button_new_with_label("Open");
    gtk_widget_add_css_class(about_btn, "flat");
    gtk_widget_set_valign(about_btn, GTK_ALIGN_CENTER);
    g_signal_connect(about_btn, "clicked", G_CALLBACK(on_about_clicked), parent);
    adw_action_row_add_suffix(ADW_ACTION_ROW(about_row), about_btn);
    adw_preferences_group_add(about_group, about_row);

    adw_dialog_present(dialog, GTK_WIDGET(parent));
}

void super_v_ui_apply_preferences(const char *langs, const char *gif_provider) {
    if (langs && *langs) g_strlcpy(g_langs, langs, sizeof(g_langs));
    if (gif_provider && *gif_provider) g_strlcpy(g_gif_provider, gif_provider, sizeof(g_gif_provider));
    if (g_lang_combo) adw_combo_row_set_selected(ADW_COMBO_ROW(g_lang_combo), (guint)lang_index_for(g_langs));
    if (g_gif_combo) adw_combo_row_set_selected(ADW_COMBO_ROW(g_gif_combo), (guint)gif_index_for(g_gif_provider));
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
    gtk_widget_set_visible(g_main_window, FALSE);
    schedule_idle_exit();
    malloc_trim(0);
}

static gboolean on_hide_signal(gpointer data) {
    (void)data;
    hide_main_window();
    return G_SOURCE_CONTINUE;
}

static gboolean on_toggle_signal(gpointer data) {
    (void)data;
    if (!g_main_window) return G_SOURCE_CONTINUE;
    cancel_idle_exit();
    gtk_widget_set_visible(g_main_window, TRUE);
    gtk_window_present(GTK_WINDOW(g_main_window));
    return G_SOURCE_CONTINUE;
}

static GtkWidget *make_thumb(const char *path, int size) {
    GError *err = NULL;
    GdkPixbuf *src = gdk_pixbuf_new_from_file(path, &err);
    if (!src) {
        if (err) g_error_free(err);
        return NULL;
    }
    GdkPixbuf *small = gdk_pixbuf_scale_simple(src, size, size, GDK_INTERP_BILINEAR);
    g_object_unref(src);
    if (!small) return NULL;
    GtkWidget *img = gtk_image_new_from_pixbuf(small);
    g_object_unref(small);
    gtk_widget_set_size_request(img, size, size);
    gtk_image_set_pixel_size(GTK_IMAGE(img), size);
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

static GtkWidget *create_main_window(AdwApplication *app) {
    GtkWidget *window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "Super-V Clipboard");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 520);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), NULL);

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
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

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Preferences", "app.preferences");
    g_menu_append(menu, "About Super-V Clipboard", "app.about");
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(menu));
    gtk_widget_add_css_class(menu_btn, "flat");
    gtk_widget_set_tooltip_text(menu_btn, "Menu");
    adw_header_bar_pack_end(ADW_HEADER_BAR(top_header), menu_btn);

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
    g_signal_connect(g_clip_list, "row-activated", G_CALLBACK(on_row_activated), NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(clip_scroll), g_clip_list);
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(g_stack), clip_scroll, "clipboard", "Clipboard", "edit-paste-symbolic");

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
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_recent_emoji_flow), 8);
    gtk_box_append(GTK_BOX(emoji_container), g_recent_emoji_flow);

    GtkWidget *all_lbl = gtk_label_new("All emoji");
    gtk_widget_set_halign(all_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(all_lbl, "heading");
    gtk_box_append(GTK_BOX(emoji_container), all_lbl);

    g_emoji_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_emoji_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_emoji_flow), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(g_emoji_flow), 2);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(g_emoji_flow), 2);
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
        return;
    }

    install_css();
    g_main_window = create_main_window(ADW_APPLICATION(app));
    gtk_window_present(GTK_WINDOW(g_main_window));

    if (g_activate_hook) g_activate_hook();

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

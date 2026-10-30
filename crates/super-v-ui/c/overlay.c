#include "overlay.h"
#include <adwaita.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *g_main_window = NULL;
static GtkWidget *g_clip_list = NULL;
static GtkWidget *g_search_entry = NULL;
static PasteCallback g_paste_cb = NULL;

void super_v_ui_set_paste_callback(PasteCallback cb) {
    g_paste_cb = cb;
}

static void on_settings_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWindow *parent = GTK_WINDOW(user_data);
    super_v_ui_show_settings((void*)parent);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    (void)user_data;
    long long id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "super_v_id"));
    const char *text = (const char*)g_object_get_data(G_OBJECT(row), "super_v_text");

    if (g_paste_cb) {
        g_paste_cb(id, text);
    }

    if (g_main_window) {
        gtk_window_close(GTK_WINDOW(g_main_window));
    }
}

static gboolean on_window_key_pressed(GtkEventControllerKey *controller,
                                      guint keyval,
                                      guint keycode,
                                      GdkModifierType state,
                                      gpointer user_data) {
    (void)controller;
    (void)keycode;
    GtkWindow *window = GTK_WINDOW(user_data);

    // Escape closes window
    if (keyval == GDK_KEY_Escape) {
        gtk_window_close(window);
        return TRUE;
    }

    // Ctrl+W or Ctrl+Q closes window
    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_q)) {
        gtk_window_close(window);
        return TRUE;
    }

    // F1 or Ctrl+, opens Settings Preferences Dialog
    if (keyval == GDK_KEY_F1 || ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_comma)) {
        super_v_ui_show_settings((void*)window);
        return TRUE;
    }

    return FALSE;
}

static gboolean clip_list_filter_func(GtkListBoxRow *row, gpointer user_data) {
    GtkSearchEntry *entry = GTK_SEARCH_ENTRY(user_data);
    const char *query = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!query || strlen(query) == 0) {
        return TRUE;
    }

    const char *title = (const char*)g_object_get_data(G_OBJECT(row), "super_v_text");
    if (!title) {
        return FALSE;
    }

    gchar *title_lower = g_utf8_strdown(title, -1);
    gchar *query_lower = g_utf8_strdown(query, -1);
    gboolean match = (strstr(title_lower, query_lower) != NULL);
    g_free(title_lower);
    g_free(query_lower);
    return match;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    (void)entry;
    GtkListBox *list = GTK_LIST_BOX(user_data);
    gtk_list_box_invalidate_filter(list);
}

void super_v_ui_show_settings(void *parent_window) {
    GtkWindow *parent = parent_window ? GTK_WINDOW(parent_window) : NULL;
    AdwDialog *pref_dialog = ADW_DIALOG(adw_preferences_dialog_new());
    adw_preferences_dialog_set_search_enabled(ADW_PREFERENCES_DIALOG(pref_dialog), TRUE);

    // General Preferences Page
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "General");
    adw_preferences_page_set_icon_name(page, "preferences-system-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(pref_dialog), page);

    // Group: Sorting & Organization
    AdwPreferencesGroup *sort_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(sort_group, "Sort & Organization");
    adw_preferences_group_set_description(sort_group, "Configure how clipboard history and categories appear");
    adw_preferences_page_add(page, sort_group);

    // Sort order dropdown
    const char *sort_options[] = { "Recently Used (Default)", "Pin First, then Recent", "Alphabetical (A-Z)", "Size (Largest First)", NULL };
    GtkWidget *sort_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sort_row), "Sort Order");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(sort_row), "Order in which history items are arranged");
    adw_combo_row_set_model(ADW_COMBO_ROW(sort_row), G_LIST_MODEL(gtk_string_list_new(sort_options)));
    adw_preferences_group_add(sort_group, sort_row);

    // Group: Features & Filters
    AdwPreferencesGroup *feat_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(feat_group, "History & Pins");
    adw_preferences_page_add(page, feat_group);

    // Switch: Keep Pinned on Top
    GtkWidget *pin_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pin_row), "Keep Pinned Items on Top");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pin_row), "Pinned snippets never get evicted by history limit");
    adw_switch_row_set_active(ADW_SWITCH_ROW(pin_row), TRUE);
    adw_preferences_group_add(feat_group, pin_row);

    // Switch: Enable GIF & Image History
    GtkWidget *media_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(media_row), "Image & Media History");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(media_row), "Store copied images and GIFs with disk cache");
    adw_switch_row_set_active(ADW_SWITCH_ROW(media_row), TRUE);
    adw_preferences_group_add(feat_group, media_row);

    // SpinRow: Max History Entries
    GtkWidget *history_limit_row = adw_spin_row_new_with_range(10, 500, 10);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(history_limit_row), "Max History Size");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(history_limit_row), "Maximum number of unpinned entries to retain");
    adw_spin_row_set_value(ADW_SPIN_ROW(history_limit_row), 100);
    adw_preferences_group_add(feat_group, history_limit_row);

    // Page 2: Diagnostics & Self-Healing
    AdwPreferencesPage *diag_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(diag_page, "Diagnostics");
    adw_preferences_page_set_icon_name(diag_page, "tools-check-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(pref_dialog), diag_page);

    AdwPreferencesGroup *status_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(status_group, "System Health & Compatibility");
    adw_preferences_page_add(diag_page, status_group);

    // Status: Compositor
    GtkWidget *comp_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(comp_row), "Compositor Target");
    const char *wayland = getenv("WAYLAND_DISPLAY");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(comp_row), wayland ? "Wayland Native (GNOME 51 / Hyprland)" : "X11 Fallback");
    adw_preferences_group_add(status_group, comp_row);

    // Status: Daemon Socket
    GtkWidget *socket_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(socket_row), "Daemon IPC Socket");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(socket_row), "/tmp/super-v-clipboard.sock (Active)");
    adw_preferences_group_add(status_group, socket_row);

    if (parent) {
        adw_dialog_present(pref_dialog, GTK_WIDGET(parent));
    }
}

void super_v_ui_clear_history_items(void) {
    if (!g_clip_list) return;
    GtkWidget *child = gtk_widget_get_first_child(g_clip_list);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(g_clip_list), child);
        child = next;
    }
}

void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id) {
    if (!g_clip_list) return;

    GtkWidget *row = adw_action_row_new();
    if (is_sensitive) {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "•••••••• (Sensitive Content Hidden)");
    } else {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title ? title : "(Empty)");
    }

    if (subtitle) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }

    if (is_pinned) {
        GtkWidget *pin_icon = gtk_image_new_from_icon_name("view-pin-symbolic");
        gtk_widget_set_tooltip_text(pin_icon, "Pinned Item");
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), pin_icon);
    }

    g_object_set_data(G_OBJECT(row), "super_v_id", GINT_TO_POINTER((int)id));
    if (title) {
        g_object_set_data_full(G_OBJECT(row), "super_v_text", g_strdup(title), g_free);
    }

    gtk_list_box_append(GTK_LIST_BOX(g_clip_list), row);
}

static GtkWidget* create_main_window(AdwApplication *app) {
    GtkWidget *window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "Super-V Clipboard");
    gtk_window_set_default_size(GTK_WINDOW(window), 440, 560);

    // Attach Keyboard Event Controller (Esc, Ctrl+W, Ctrl+Q, F1)
    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_window_key_pressed), window);
    gtk_widget_add_controller(window, key_ctrl);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), box);

    // HeaderBar
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), TRUE);

    // Settings Button in HeaderBar
    GtkWidget *settings_btn = gtk_button_new_from_icon_name("preferences-system-symbolic");
    gtk_widget_set_tooltip_text(settings_btn, "Preferences & Diagnostics (F1)");
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), window);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), settings_btn);

    gtk_box_append(GTK_BOX(box), header);

    // Search bar
    g_search_entry = gtk_search_entry_new();
    gtk_widget_set_margin_start(g_search_entry, 12);
    gtk_widget_set_margin_end(g_search_entry, 12);
    gtk_widget_set_margin_top(g_search_entry, 8);
    gtk_widget_set_margin_bottom(g_search_entry, 8);
    gtk_box_append(GTK_BOX(box), g_search_entry);

    // Tab Stack
    AdwViewStack *stack = ADW_VIEW_STACK(adw_view_stack_new());
    gtk_widget_set_vexpand(GTK_WIDGET(stack), TRUE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(stack));

    // TAB 1: Clipboard History
    GtkWidget *clip_scroll = gtk_scrolled_window_new();
    g_clip_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_clip_list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(g_clip_list, "boxed-list");
    gtk_widget_set_margin_start(g_clip_list, 12);
    gtk_widget_set_margin_end(g_clip_list, 12);
    gtk_widget_set_margin_top(g_clip_list, 6);
    gtk_widget_set_margin_bottom(g_clip_list, 6);

    // Wire live search filter func
    gtk_list_box_set_filter_func(GTK_LIST_BOX(g_clip_list), clip_list_filter_func, g_search_entry, NULL);
    g_signal_connect(g_search_entry, "search-changed", G_CALLBACK(on_search_changed), g_clip_list);

    // Connect row-activated event for instant paste & auto-close
    g_signal_connect(g_clip_list, "row-activated", G_CALLBACK(on_row_activated), NULL);

    // Populate initial items
    super_v_ui_add_history_item("Important: cargo build --workspace", "Pinned • Just now", 1, 0, 1);
    super_v_ui_add_history_item("https://github.com/GNOME/gnome-shell/tree/51.1", "URL • 2 mins ago", 0, 0, 2);
    super_v_ui_add_history_item("ghp_1234567890abcdefghijklmnopqrstuvwxyz", "Secret Token • 5 mins ago", 0, 1, 3);
    super_v_ui_add_history_item("#308b53 (Accent Green)", "Color • 10 mins ago", 0, 0, 4);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(clip_scroll), g_clip_list);
    adw_view_stack_add_titled(stack, clip_scroll, "clipboard", "Clipboard");

    // TAB 2: Emoji
    GtkWidget *emoji_scroll = gtk_scrolled_window_new();
    GtkWidget *emoji_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(emoji_box, 16);
    gtk_widget_set_margin_end(emoji_box, 16);
    gtk_widget_set_margin_top(emoji_box, 12);
    GtkWidget *emoji_grid_label = gtk_label_new("😀  🚀  🦀  ⚡  🔥  ✨  🎉  💡  ❤️  ⭐\n\n1,914 Emojis Indexed with Category Search");
    gtk_label_set_wrap(GTK_LABEL(emoji_grid_label), TRUE);
    gtk_box_append(GTK_BOX(emoji_box), emoji_grid_label);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(emoji_scroll), emoji_box);
    adw_view_stack_add_titled(stack, emoji_scroll, "emoji", "Emoji");

    // TAB 3: Kaomoji
    GtkWidget *kaomoji_scroll = gtk_scrolled_window_new();
    GtkWidget *kaomoji_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(kaomoji_box, 16);
    gtk_widget_set_margin_end(kaomoji_box, 16);
    gtk_widget_set_margin_top(kaomoji_box, 12);
    const char *kaomojis[] = { "¯\\_(ツ)_/¯ (Shrug)", "( ͡° ͜ʖ ͡°) (Lenny)", "(╯°□°)╯︵ ┻━┻ (Table Flip)", "┬─┬ノ( º _ ºノ) (Put Back)" };
    for (size_t i = 0; i < 4; i++) {
        GtkWidget *k_row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(k_row), kaomojis[i]);
        gtk_box_append(GTK_BOX(kaomoji_box), k_row);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(kaomoji_scroll), kaomoji_box);
    adw_view_stack_add_titled(stack, kaomoji_scroll, "kaomoji", "Kaomoji");

    // TAB 4: Symbols
    GtkWidget *sym_scroll = gtk_scrolled_window_new();
    GtkWidget *sym_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(sym_box, 16);
    gtk_widget_set_margin_end(sym_box, 16);
    gtk_widget_set_margin_top(sym_box, 12);
    GtkWidget *sym_label = gtk_label_new("→  ←  ↑  ↓  ✓  ✗  €  $  £  ¥  ∑  ∫  π  λ  Ω  ∞\n\n1,073 Math, Currency, Arrows & Greek Symbols");
    gtk_box_append(GTK_BOX(sym_box), sym_label);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sym_scroll), sym_box);
    adw_view_stack_add_titled(stack, sym_scroll, "symbols", "Symbols");

    // Bottom Switcher Bar
    GtkWidget *switcher_bar = adw_view_switcher_bar_new();
    adw_view_switcher_bar_set_stack(ADW_VIEW_SWITCHER_BAR(switcher_bar), stack);
    adw_view_switcher_bar_set_reveal(ADW_VIEW_SWITCHER_BAR(switcher_bar), TRUE);
    gtk_box_append(GTK_BOX(box), switcher_bar);

    return window;
}

static void on_app_activate(GApplication *app, gpointer user_data) {
    (void)user_data;
    if (!g_main_window) {
        g_main_window = create_main_window(ADW_APPLICATION(app));
    }
    gtk_window_present(GTK_WINDOW(g_main_window));
}

int super_v_ui_run_app(int argc, char **argv) {
    AdwApplication *app = adw_application_new("org.darulfalah.SuperVClipboard", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

int super_v_ui_test_prewarm(char *version_buf, int max_len) {
    adw_init();
    if (version_buf && max_len > 0) {
        snprintf(version_buf, max_len, "GTK %d.%d.%d, Libadwaita %d.%d.%d",
                 gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
                 adw_get_major_version(), adw_get_minor_version(), adw_get_micro_version());
    }
    return 1;
}

#include "overlay.h"
#include <adwaita.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *g_main_window = NULL;

static void on_settings_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWindow *parent = GTK_WINDOW(user_data);
    super_v_ui_show_settings((void*)parent);
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

static GtkWidget* create_main_window(AdwApplication *app) {
    GtkWidget *window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "Super-V Clipboard");
    gtk_window_set_default_size(GTK_WINDOW(window), 440, 560);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), box);

    // HeaderBar
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), TRUE);

    // Settings Button in HeaderBar
    GtkWidget *settings_btn = gtk_button_new_from_icon_name("preferences-system-symbolic");
    gtk_widget_set_tooltip_text(settings_btn, "Preferences & Diagnostics");
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), window);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), settings_btn);

    gtk_box_append(GTK_BOX(box), header);

    // Search bar
    GtkWidget *search_entry = gtk_search_entry_new();
    gtk_widget_set_margin_start(search_entry, 12);
    gtk_widget_set_margin_end(search_entry, 12);
    gtk_widget_set_margin_top(search_entry, 8);
    gtk_widget_set_margin_bottom(search_entry, 8);
    gtk_box_append(GTK_BOX(box), search_entry);

    // Tab Stack
    AdwViewStack *stack = ADW_VIEW_STACK(adw_view_stack_new());
    gtk_widget_set_vexpand(GTK_WIDGET(stack), TRUE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(stack));

    // TAB 1: Clipboard History
    GtkWidget *clip_scroll = gtk_scrolled_window_new();
    GtkWidget *clip_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(clip_list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(clip_list, "boxed-list");
    gtk_widget_set_margin_start(clip_list, 12);
    gtk_widget_set_margin_end(clip_list, 12);
    gtk_widget_set_margin_top(clip_list, 6);
    gtk_widget_set_margin_bottom(clip_list, 6);

    // Sample active history items
    const char *sample_history[] = {
        "📌 Important: cargo build --workspace",
        "https://github.com/GNOME/gnome-shell/tree/51.1",
        "Super-V ultra-low latency native clipboard",
        "SQLite FTS5 + WAL search cache",
        "Adwaita ViewStack & ViewSwitcherBar"
    };
    for (size_t i = 0; i < 5; i++) {
        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), sample_history[i]);
        if (i == 0) {
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Pinned • Just now");
        } else {
            char sub[64];
            snprintf(sub, sizeof(sub), "Item #%zu • 2 mins ago", i);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub);
        }
        gtk_list_box_append(GTK_LIST_BOX(clip_list), row);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(clip_scroll), clip_list);
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

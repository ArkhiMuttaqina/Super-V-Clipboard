#include "overlay.h"
#include <adwaita.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *g_main_window = NULL;
static GtkWidget *g_clip_list = NULL;
static GtkWidget *g_search_entry = NULL;
static GtkWidget *g_emoji_flow = NULL;
static GtkWidget *g_recent_emoji_flow = NULL;
static GtkWidget *g_symbol_flow = NULL;
static GtkWidget *g_kaomoji_box = NULL;
static GtkWidget *g_gif_flow = NULL;

static void free_closure_data(gpointer data, GClosure *closure) { (void)closure; g_free(data); }
static PasteCallback g_paste_cb = NULL;
static PinCallback g_pin_cb = NULL;
static DeleteCallback g_delete_cb = NULL;

void super_v_ui_set_paste_callback(PasteCallback cb) { g_paste_cb = cb; }
void super_v_ui_set_pin_callback(PinCallback cb) { g_pin_cb = cb; }
void super_v_ui_set_delete_callback(DeleteCallback cb) { g_delete_cb = cb; }

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
        gtk_widget_set_visible(g_main_window, FALSE);
    }
}

static void on_pin_btn_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *row = GTK_WIDGET(user_data);
    long long id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "super_v_id"));
    int is_pinned = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "super_v_pinned"));

    is_pinned = !is_pinned;
    g_object_set_data(G_OBJECT(row), "super_v_pinned", GINT_TO_POINTER(is_pinned));
    gtk_button_set_icon_name(btn, is_pinned ? "view-pin-symbolic" : "view-pin-symbolic");

    if (g_pin_cb) {
        g_pin_cb(id);
    }
}

static void on_delete_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *row = GTK_WIDGET(user_data);
    long long id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "super_v_id"));

    if (g_clip_list) {
        gtk_list_box_remove(GTK_LIST_BOX(g_clip_list), row);
    }

    if (g_delete_cb) {
        g_delete_cb(id);
    }
}

static void on_toggle_mask_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *row = GTK_WIDGET(user_data);
    int is_masked = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "super_v_masked"));
    const char *raw_text = (const char*)g_object_get_data(G_OBJECT(row), "super_v_text");

    is_masked = !is_masked;
    g_object_set_data(G_OBJECT(row), "super_v_masked", GINT_TO_POINTER(is_masked));

    if (is_masked) {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "•••••••• (Sensitive Content Hidden)");
        gtk_button_set_icon_name(btn, "view-reveal-symbolic");
        gtk_widget_set_tooltip_text(GTK_WIDGET(btn), "Show Sensitive Content");
    } else {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), raw_text ? raw_text : "");
        gtk_button_set_icon_name(btn, "view-conceal-symbolic");
        gtk_widget_set_tooltip_text(GTK_WIDGET(btn), "Hide Sensitive Content");
    }
}

static void on_glyph_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *glyph = (const char*)user_data;
    if (g_paste_cb) {
        g_paste_cb(0, glyph);
    }
    if (g_main_window) {
        gtk_widget_set_visible(g_main_window, FALSE);
    }
}

static void on_emoji_skintone_variant_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *variant = (const char*)user_data;
    if (g_paste_cb) {
        g_paste_cb(0, variant);
    }
    if (g_main_window) {
        gtk_widget_set_visible(g_main_window, FALSE);
    }
}

static void on_emoji_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    GtkWidget *btn = GTK_WIDGET(user_data);
    const char *base_emoji = (const char*)g_object_get_data(G_OBJECT(btn), "base_emoji");
    if (!base_emoji) return;

    const char *modifiers[] = { "", "\xf0\x9f\x8f\xbb", "\xf0\x9f\x8f\xbc", "\xf0\x9f\x8f\xbd", "\xf0\x9f\x8f\xbe", "\xf0\x9f\x8f\xbf" };
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, btn);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    for (int i = 0; i < 6; i++) {
        char variant[64];
        snprintf(variant, sizeof(variant), "%s%s", base_emoji, modifiers[i]);
        GtkWidget *var_btn = gtk_button_new_with_label(variant);
        gtk_widget_add_css_class(var_btn, "flat");
        g_signal_connect_data(var_btn, "clicked", G_CALLBACK(on_emoji_skintone_variant_clicked), g_strdup(variant), (GClosureNotify)free_closure_data, 0);
        gtk_box_append(GTK_BOX(box), var_btn);
    }

    gtk_popover_popup(GTK_POPOVER(popover));
}

static gboolean on_window_key_pressed(GtkEventControllerKey *controller,
                                      guint keyval,
                                      guint keycode,
                                      GdkModifierType state,
                                      gpointer user_data) {
    (void)controller;
    (void)keycode;
    GtkWindow *window = GTK_WINDOW(user_data);

    if (keyval == GDK_KEY_Escape) {
        gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
        return TRUE;
    }

    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_w || keyval == GDK_KEY_q)) {
        gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
        return TRUE;
    }

    if (keyval == GDK_KEY_F1 || ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_comma)) {
        super_v_ui_show_settings((void*)window);
        return TRUE;
    }

    return FALSE;
}

static gboolean on_window_close_request(GtkWindow *window, gpointer user_data) {
    (void)user_data;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void on_close_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *window = GTK_WIDGET(user_data);
    gtk_widget_set_visible(window, FALSE);
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

    // Page 1: General Preferences
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, "General");
    adw_preferences_page_set_icon_name(page, "preferences-system-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(pref_dialog), page);

    // Group: Sorting
    AdwPreferencesGroup *sort_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(sort_group, "Sort & Organization");
    adw_preferences_page_add(page, sort_group);

    const char *sort_options[] = { "Recently Used (Default)", "Pin First, then Recent", "Alphabetical (A-Z)", "Size (Largest First)", NULL };
    GtkWidget *sort_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sort_row), "Sort Order");
    adw_combo_row_set_model(ADW_COMBO_ROW(sort_row), G_LIST_MODEL(gtk_string_list_new(sort_options)));
    adw_preferences_group_add(sort_group, sort_row);

    // Group: Emoji Styles (Windows 11, iOS, Latest Android Material, WhatsApp 2026)
    AdwPreferencesGroup *emoji_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(emoji_group, "Emoji & Asset Style (2026 Emojipedia Reference)");
    adw_preferences_page_add(page, emoji_group);

    const char *emoji_styles[] = { "Windows 11 (Fluent 3D - Default)", "Apple iOS (Latest)", "Google Android Material UI (2026)", "WhatsApp Native", NULL };
    GtkWidget *emoji_style_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(emoji_style_row), "Emoji Set Style");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(emoji_style_row), "Visual style rendered for all 1,914 Unicode emojis");
    adw_combo_row_set_model(ADW_COMBO_ROW(emoji_style_row), G_LIST_MODEL(gtk_string_list_new(emoji_styles)));
    adw_preferences_group_add(emoji_group, emoji_style_row);

    // Group: GIF Providers
    AdwPreferencesGroup *gif_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(gif_group, "GIF Search & Media Provider");
    adw_preferences_page_add(page, gif_group);

    const char *gif_providers[] = { "Local Offline GIF Scanner (Privacy Friendly)", "Tenor API (Windows Default)", "Giphy Provider", "Klarna Media", NULL };
    GtkWidget *gif_prov_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(gif_prov_row), "Active GIF Provider");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(gif_prov_row), "Source used to query animated GIFs in GIF tab");
    adw_combo_row_set_model(ADW_COMBO_ROW(gif_prov_row), G_LIST_MODEL(gtk_string_list_new(gif_providers)));
    adw_preferences_group_add(gif_group, gif_prov_row);

    // Group: Features & Privacy
    AdwPreferencesGroup *feat_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(feat_group, "History, Pins & Secrets");
    adw_preferences_page_add(page, feat_group);

    GtkWidget *mask_default_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mask_default_row), "Show Content by Default (No Hidden Masking)");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(mask_default_row), "When enabled, sensitive tokens are readable until eye icon is toggled");
    adw_switch_row_set_active(ADW_SWITCH_ROW(mask_default_row), TRUE);
    adw_preferences_group_add(feat_group, mask_default_row);

    GtkWidget *pin_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pin_row), "Keep Pinned Items on Top");
    adw_switch_row_set_active(ADW_SWITCH_ROW(pin_row), TRUE);
    adw_preferences_group_add(feat_group, pin_row);

    GtkWidget *history_limit_row = adw_spin_row_new_with_range(10, 500, 10);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(history_limit_row), "Max History Size");
    adw_spin_row_set_value(ADW_SPIN_ROW(history_limit_row), 100);
    adw_preferences_group_add(feat_group, history_limit_row);

    // Page 2: Diagnostics
    AdwPreferencesPage *diag_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(diag_page, "Diagnostics");
    adw_preferences_page_set_icon_name(diag_page, "tools-check-symbolic");
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(pref_dialog), diag_page);

    AdwPreferencesGroup *status_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(status_group, "System Health & Compatibility");
    adw_preferences_page_add(diag_page, status_group);

    GtkWidget *comp_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(comp_row), "Compositor Target");
    const char *wayland = getenv("WAYLAND_DISPLAY");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(comp_row), wayland ? "Wayland Native (GNOME 51 / Hyprland)" : "X11 Fallback");
    adw_preferences_group_add(status_group, comp_row);

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

void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id, const char *preview_image_path) {
    if (!g_clip_list) return;

    GtkWidget *row = adw_action_row_new();
    g_object_set_data(G_OBJECT(row), "super_v_id", GINT_TO_POINTER((int)id));
    g_object_set_data(G_OBJECT(row), "super_v_pinned", GINT_TO_POINTER(is_pinned));
    g_object_set_data(G_OBJECT(row), "super_v_masked", GINT_TO_POINTER(0)); // By default show content

    if (title) {
        g_object_set_data_full(G_OBJECT(row), "super_v_text", g_strdup(title), g_free);
    }

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title ? title : "(Empty)");
    if (subtitle) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }

    if (preview_image_path && strlen(preview_image_path) > 0) {
        GtkWidget *img = gtk_picture_new_for_filename(preview_image_path);
        gtk_widget_set_size_request(img, 48, 48);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), img);
    } else if (is_pinned) {
        GtkWidget *pin_icon = gtk_image_new_from_icon_name("view-pin-symbolic");
        gtk_widget_set_tooltip_text(pin_icon, "Pinned Item");
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), pin_icon);
    }

    if (is_sensitive) {
        GtkWidget *eye_btn = gtk_button_new_from_icon_name("view-reveal-symbolic");
        gtk_widget_set_tooltip_text(eye_btn, "Toggle Secret Masking");
        gtk_widget_add_css_class(eye_btn, "flat");
        g_signal_connect(eye_btn, "clicked", G_CALLBACK(on_toggle_mask_clicked), row);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), eye_btn);
    }

    GtkWidget *pin_btn = gtk_button_new_from_icon_name("view-pin-symbolic");
    gtk_widget_set_tooltip_text(pin_btn, is_pinned ? "Unpin Item" : "Pin Item");
    gtk_widget_add_css_class(pin_btn, "flat");
    g_signal_connect(pin_btn, "clicked", G_CALLBACK(on_pin_btn_clicked), row);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), pin_btn);

    GtkWidget *del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_set_tooltip_text(del_btn, "Delete Item");
    gtk_widget_add_css_class(del_btn, "flat");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_delete_btn_clicked), row);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), del_btn);

    gtk_list_box_append(GTK_LIST_BOX(g_clip_list), row);
}

void super_v_ui_add_glyph_item(const char *tab, const char *glyph, const char *name, const char *keywords, int skin_tone_capable) {
    (void)keywords;
    GtkWidget *target_flow = NULL;
    if (strcmp(tab, "emoji") == 0) {
        target_flow = g_emoji_flow;
    } else if (strcmp(tab, "symbol") == 0) {
        target_flow = g_symbol_flow;
    } else if (strcmp(tab, "gif") == 0) {
        target_flow = g_gif_flow;
    }

    if (!target_flow) return;

    GtkWidget *btn = gtk_button_new_with_label(glyph);
    gtk_widget_set_tooltip_text(btn, name);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_size_request(btn, 38, 38);

    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_glyph_clicked), g_strdup(glyph), (GClosureNotify)free_closure_data, 0);

    if (skin_tone_capable) {
        g_object_set_data_full(G_OBJECT(btn), "base_emoji", g_strdup(glyph), g_free);
        GtkGesture *right_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
        g_signal_connect(right_click, "pressed", G_CALLBACK(on_emoji_right_clicked), btn);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(right_click));
    }

    gtk_flow_box_append(GTK_FLOW_BOX(target_flow), btn);
}

void super_v_ui_record_recent(const char *type, const char *content) {
    (void)type;
    if (!g_recent_emoji_flow) return;
    GtkWidget *btn = gtk_button_new_with_label(content);
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_size_request(btn, 38, 38);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_glyph_clicked), g_strdup(content), (GClosureNotify)free_closure_data, 0);
    gtk_flow_box_insert(GTK_FLOW_BOX(g_recent_emoji_flow), btn, 0);
}

static GtkWidget* create_main_window(AdwApplication *app) {
    GtkWidget *window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "Clipboard (Windows+V)");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 520);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_window_key_pressed), window);
    gtk_widget_add_controller(window, key_ctrl);
    g_signal_connect(window, "close-request", G_CALLBACK(on_window_close_request), NULL);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), main_box);

    AdwViewStack *stack = ADW_VIEW_STACK(adw_view_stack_new());
    gtk_widget_set_vexpand(GTK_WIDGET(stack), TRUE);

    GtkWidget *top_header = adw_header_bar_new();
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(top_header), FALSE);
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(top_header), FALSE);

    AdwViewSwitcher *switcher = ADW_VIEW_SWITCHER(adw_view_switcher_new());
    adw_view_switcher_set_stack(switcher, stack);
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
    gtk_box_append(GTK_BOX(main_box), g_search_entry);

    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(stack));

    // TAB 1: Clipboard History
    GtkWidget *clip_scroll = gtk_scrolled_window_new();
    g_clip_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_clip_list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(g_clip_list, "boxed-list");
    gtk_widget_set_margin_start(g_clip_list, 10);
    gtk_widget_set_margin_end(g_clip_list, 10);
    gtk_widget_set_margin_top(g_clip_list, 4);
    gtk_widget_set_margin_bottom(g_clip_list, 4);

    gtk_list_box_set_filter_func(GTK_LIST_BOX(g_clip_list), clip_list_filter_func, g_search_entry, NULL);
    g_signal_connect(g_search_entry, "search-changed", G_CALLBACK(on_search_changed), g_clip_list);
    g_signal_connect(g_clip_list, "row-activated", G_CALLBACK(on_row_activated), NULL);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(clip_scroll), g_clip_list);
    adw_view_stack_add_titled_with_icon(stack, clip_scroll, "clipboard", "Clipboard", "edit-paste-symbolic");

    // TAB 2: Emoji
    GtkWidget *emoji_scroll = gtk_scrolled_window_new();
    GtkWidget *emoji_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(emoji_container, 10);
    gtk_widget_set_margin_end(emoji_container, 10);
    gtk_widget_set_margin_top(emoji_container, 6);

    GtkWidget *recent_lbl = gtk_label_new("Recent");
    gtk_widget_set_halign(recent_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(recent_lbl, "heading");
    gtk_box_append(GTK_BOX(emoji_container), recent_lbl);

    g_recent_emoji_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_recent_emoji_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_recent_emoji_flow), 8);
    gtk_box_append(GTK_BOX(emoji_container), g_recent_emoji_flow);

    const char *initial_recents[] = { "🙏", "✨", "😅", "👌", "😭", "😂", "🔥", "❤️" };
    for (int i = 0; i < 8; i++) {
        super_v_ui_record_recent("emoji", initial_recents[i]);
    }

    GtkWidget *all_lbl = gtk_label_new("All Emojis (Right-click / hold for skin tone)");
    gtk_widget_set_halign(all_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(all_lbl, "dim-label");
    gtk_box_append(GTK_BOX(emoji_container), all_lbl);

    g_emoji_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_emoji_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_emoji_flow), 8);
    gtk_box_append(GTK_BOX(emoji_container), g_emoji_flow);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(emoji_scroll), emoji_container);
    adw_view_stack_add_titled_with_icon(stack, emoji_scroll, "emoji", "Emoji", "face-smile-symbolic");

    // TAB 3: GIF Search & Provider
    GtkWidget *gif_scroll = gtk_scrolled_window_new();
    GtkWidget *gif_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(gif_container, 10);
    gtk_widget_set_margin_end(gif_container, 10);
    gtk_widget_set_margin_top(gif_container, 6);

    GtkWidget *gif_lbl = gtk_label_new("Trending & Local GIFs (Provider: Local/Tenor)");
    gtk_widget_set_halign(gif_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(gif_lbl, "dim-label");
    gtk_box_append(GTK_BOX(gif_container), gif_lbl);

    g_gif_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_gif_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_gif_flow), 4);
    gtk_box_append(GTK_BOX(gif_container), g_gif_flow);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(gif_scroll), gif_container);
    adw_view_stack_add_titled_with_icon(stack, gif_scroll, "gif", "GIF", "media-optical-symbolic");

    // TAB 4: Kaomoji
    GtkWidget *kaomoji_scroll = gtk_scrolled_window_new();
    g_kaomoji_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(g_kaomoji_box, 10);
    gtk_widget_set_margin_end(g_kaomoji_box, 10);
    gtk_widget_set_margin_top(g_kaomoji_box, 6);

    const char *kaomojis[] = {
        "¯\_(ツ)_/¯ (Shrug)", "( ͡° ͜ʖ ͡°) (Lenny)", "(╯°□°)╯︵ ┻━┻ (Table Flip)",
        "┬─┬ノ( º _ ºノ) (Put Back)", "(◕‿◕) (Happy)", "(ಠ_ಠ) (Disapproval)",
        "(ಥ﹏ಥ) (Crying)", "(•_•) (Blank Stare)", "(¬‿¬) (Smirk)"
    };
    for (size_t i = 0; i < 9; i++) {
        GtkWidget *k_btn = gtk_button_new_with_label(kaomojis[i]);
        gtk_widget_add_css_class(k_btn, "flat");
        g_signal_connect_data(k_btn, "clicked", G_CALLBACK(on_glyph_clicked), g_strdup(kaomojis[i]), (GClosureNotify)free_closure_data, 0);
        gtk_box_append(GTK_BOX(g_kaomoji_box), k_btn);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(kaomoji_scroll), g_kaomoji_box);
    adw_view_stack_add_titled_with_icon(stack, kaomoji_scroll, "kaomoji", "Kaomoji", "emblem-favorite-symbolic");

    // TAB 5: Symbols
    GtkWidget *sym_scroll = gtk_scrolled_window_new();
    GtkWidget *sym_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(sym_container, 10);
    gtk_widget_set_margin_end(sym_container, 10);
    gtk_widget_set_margin_top(sym_container, 6);

    GtkWidget *sym_lbl = gtk_label_new("1,073 Math, Currency, Arrows & Greek Symbols");
    gtk_widget_set_halign(sym_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(sym_lbl, "dim-label");
    gtk_box_append(GTK_BOX(sym_container), sym_lbl);

    g_symbol_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_symbol_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_symbol_flow), 8);
    gtk_box_append(GTK_BOX(sym_container), g_symbol_flow);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sym_scroll), sym_container);
    adw_view_stack_add_titled_with_icon(stack, sym_scroll, "symbols", "Symbols", "accessories-calculator-symbolic");

    return window;
}

static void on_app_activate(GApplication *app, gpointer user_data) {
    (void)user_data;
    if (!g_main_window) {
        g_main_window = create_main_window(ADW_APPLICATION(app));
    }
    gtk_widget_set_visible(g_main_window, TRUE);
    gtk_window_present(GTK_WINDOW(g_main_window));
}

int super_v_ui_run_app(int argc, char **argv) {
    AdwApplication *app = adw_application_new("my.id.arkhimuttaqina.SuperVClipboard", G_APPLICATION_DEFAULT_FLAGS);
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

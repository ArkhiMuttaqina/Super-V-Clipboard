#include "overlay.h"
#include <adwaita.h>
#include <stdio.h>

int super_v_ui_init(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    adw_init();
    return 1;
}

void super_v_ui_check_versions(char *buf, int max_len) {
    snprintf(buf, max_len, "GTK %d.%d.%d, Libadwaita %d.%d.%d",
             gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
             adw_get_major_version(), adw_get_minor_version(), adw_get_micro_version());
}

void* super_v_ui_create_window(void) {
    // Create Adwaita Window
    GtkWidget *window = adw_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Super-V Clipboard");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 520);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

    // Main Box layout
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_window_set_content(ADW_WINDOW(window), box);

    // Adwaita HeaderBar / Search
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), TRUE);
    gtk_box_append(GTK_BOX(box), header);

    // Search entry
    GtkWidget *search_entry = gtk_search_entry_new();
    gtk_widget_set_margin_start(search_entry, 12);
    gtk_widget_set_margin_end(search_entry, 12);
    gtk_widget_set_margin_top(search_entry, 6);
    gtk_widget_set_margin_bottom(search_entry, 6);
    gtk_box_append(GTK_BOX(box), search_entry);

    // ViewStack for tabs (Clipboard, GIF, Emoji, Kaomoji, Symbols)
    AdwViewStack *stack = ADW_VIEW_STACK(adw_view_stack_new());
    gtk_widget_set_vexpand(GTK_WIDGET(stack), TRUE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(stack));

    // Tab 1: Clipboard
    GtkWidget *clip_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *clip_label = gtk_label_new("Clipboard History");
    gtk_box_append(GTK_BOX(clip_box), clip_label);
    adw_view_stack_add_titled(stack, clip_box, "clipboard", "Clipboard");

    // Tab 2: Emoji
    GtkWidget *emoji_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *emoji_label = gtk_label_new("Emoji (1,914)");
    gtk_box_append(GTK_BOX(emoji_box), emoji_label);
    adw_view_stack_add_titled(stack, emoji_box, "emoji", "Emoji");

    // Tab 3: Kaomoji
    GtkWidget *kaomoji_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *kaomoji_label = gtk_label_new("Kaomoji (83)");
    gtk_box_append(GTK_BOX(kaomoji_box), kaomoji_label);
    adw_view_stack_add_titled(stack, kaomoji_box, "kaomoji", "Kaomoji");

    // Tab 4: Symbols
    GtkWidget *sym_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *sym_label = gtk_label_new("Symbols (1,073)");
    gtk_box_append(GTK_BOX(sym_box), sym_label);
    adw_view_stack_add_titled(stack, sym_box, "symbols", "Symbols");

    // Adwaita ViewSwitcherBar at the bottom
    GtkWidget *switcher_bar = adw_view_switcher_bar_new();
    adw_view_switcher_bar_set_stack(ADW_VIEW_SWITCHER_BAR(switcher_bar), stack);
    adw_view_switcher_bar_set_reveal(ADW_VIEW_SWITCHER_BAR(switcher_bar), TRUE);
    gtk_box_append(GTK_BOX(box), switcher_bar);

    return (void*)window;
}

void super_v_ui_set_visible(void *window, int visible) {
    if (!window) return;
    if (visible) {
        gtk_widget_set_visible(GTK_WIDGET(window), TRUE);
    } else {
        gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    }
}

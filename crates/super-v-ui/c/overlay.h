#ifndef SUPER_V_OVERLAY_H
#define SUPER_V_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PasteCallback)(long long id, const char *text);
typedef void (*PinCallback)(long long id);
typedef void (*DeleteCallback)(long long id);

// Sets the global paste callback triggered when an item is chosen
void super_v_ui_set_paste_callback(PasteCallback cb);
void super_v_ui_set_pin_callback(PinCallback cb);
void super_v_ui_set_delete_callback(DeleteCallback cb);

// Runs the full Adwaita GUI Application (event loop)
int super_v_ui_run_app(int argc, char **argv);

// Non-blocking pre-warm check for benchmarks & headless testing
int super_v_ui_test_prewarm(char *version_buf, int max_len);

// Shows the settings preferences window
void super_v_ui_show_settings(void *parent_window);

// Adds a clipboard history row to the live UI list
void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id, const char *preview_image_path);

// Clears all history items from the live UI list
void super_v_ui_clear_history_items(void);

// Appends a glyph button to the respective tab grid (emoji, kaomoji, symbol, gif)
void super_v_ui_add_glyph_item(const char *tab, const char *glyph, const char *name, const char *keywords, int skin_tone_capable);

// Records a recently used item into the Recent section
void super_v_ui_record_recent(const char *type, const char *content);

#ifdef __cplusplus
}
#endif

#endif // SUPER_V_OVERLAY_H

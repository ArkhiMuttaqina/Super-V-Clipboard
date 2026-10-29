#ifndef SUPER_V_OVERLAY_H
#define SUPER_V_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

// Runs the full Adwaita GUI Application (event loop)
int super_v_ui_run_app(int argc, char **argv);

// Non-blocking pre-warm check for benchmarks & headless testing
int super_v_ui_test_prewarm(char *version_buf, int max_len);

// Shows the settings preferences window
void super_v_ui_show_settings(void *parent_window);

// Adds a clipboard history row to the live UI list
void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id);

// Clears all history items from the live UI list
void super_v_ui_clear_history_items(void);

#ifdef __cplusplus
}
#endif

#endif // SUPER_V_OVERLAY_H

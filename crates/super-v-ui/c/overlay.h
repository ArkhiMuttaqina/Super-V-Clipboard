#ifndef SUPER_V_OVERLAY_H
#define SUPER_V_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PasteCallback)(long long id, const char *text);
typedef void (*PinCallback)(long long id);
typedef void (*DeleteCallback)(long long id);

/* Lazy glyph provider: fill rows [offset, offset+limit) for the given tab.
   Must call super_v_ui_add_glyph_item() for each emitted row.
   Returns the number of rows actually emitted. */
typedef int (*GlyphProvider)(const char *tab, int offset, int limit);

/* Runs once, only in the primary instance, after the window is built. */
typedef void (*ActivateHook)(void);

void super_v_ui_set_paste_callback(PasteCallback cb);
void super_v_ui_set_pin_callback(PinCallback cb);
void super_v_ui_set_delete_callback(DeleteCallback cb);
void super_v_ui_set_glyph_provider(GlyphProvider cb);
void super_v_ui_set_activate_hook(ActivateHook cb);

int super_v_ui_run_app(int argc, char **argv);
int super_v_ui_test_prewarm(char *version_buf, int max_len);
void super_v_ui_show_settings(void *parent_window);

void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id, const char *preview_image_path);
void super_v_ui_clear_history_items(void);
void super_v_ui_add_glyph_item(const char *tab, const char *glyph, const char *name, const char *keywords, int skin_tone_capable);
void super_v_ui_record_recent(const char *type, const char *content);

#ifdef __cplusplus
}
#endif

#endif /* SUPER_V_OVERLAY_H */

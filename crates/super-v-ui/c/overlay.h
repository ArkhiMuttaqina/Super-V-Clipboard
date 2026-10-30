#ifndef SUPER_V_OVERLAY_H
#define SUPER_V_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PasteCallback)(long long id, const char *text);
typedef void (*PinCallback)(long long id);
typedef void (*DeleteCallback)(long long id);

/* Lazy glyph provider. Fills rows [offset, offset+limit) for the given tab.
   An empty/NULL query means "the whole tab"; otherwise the query is a
   localized search string. The provider must call super_v_ui_add_glyph_item()
   for each emitted row and return how many it emitted. */
typedef int (*GlyphProvider)(const char *tab, const char *query, int offset, int limit);

/* Runs once, only in the primary instance, after the window is built. */
typedef void (*ActivateHook)(void);

/* Preferences -> behaviour bridges. */
typedef void (*LanguageHook)(const char *langs);   /* e.g. "en,id" */
typedef void (*GifProviderHook)(const char *id);   /* "local", "tenor", ... */
typedef void (*GifReloadHook)(void);

void super_v_ui_set_paste_callback(PasteCallback cb);
void super_v_ui_set_pin_callback(PinCallback cb);
void super_v_ui_set_delete_callback(DeleteCallback cb);
void super_v_ui_set_glyph_provider(GlyphProvider cb);
void super_v_ui_set_activate_hook(ActivateHook cb);
void super_v_ui_set_language_hook(LanguageHook cb);
void super_v_ui_set_gif_provider_hook(GifProviderHook cb);
void super_v_ui_set_gif_reload_hook(GifReloadHook cb);

/* Called from Rust after the hooks are installed, so the C side can reflect
   the persisted preferences in its widgets. */
void super_v_ui_apply_preferences(const char *langs, const char *gif_provider);

int super_v_ui_run_app(int argc, char **argv);
int super_v_ui_test_prewarm(char *version_buf, int max_len);
void super_v_ui_show_settings(void *parent_window);
void super_v_ui_show_about(void *parent_window);

void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id, const char *preview_image_path);
void super_v_ui_clear_history_items(void);
void super_v_ui_add_glyph_item(const char *tab, const char *glyph, const char *name, const char *keywords, int skin_tone_capable);
void super_v_ui_clear_glyphs(const char *tab);
void super_v_ui_record_recent(const char *type, const char *content);
void super_v_ui_add_gif_item(const char *path, const char *title);
void super_v_ui_clear_gif_items(void);
void super_v_ui_refresh_gifs(void);
void super_v_ui_set_gif_hint(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* SUPER_V_OVERLAY_H */

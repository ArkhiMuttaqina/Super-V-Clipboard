#ifndef SUPER_V_OVERLAY_H
#define SUPER_V_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PasteCallback)(long long id, const char *text);
typedef void (*PinCallback)(long long id);
typedef void (*RefreshHook)(void);
typedef void (*UpdateHook)(void);
typedef void (*SnippetAddHook)(const char *name, const char *body);
typedef void (*SnippetDeleteHook)(long long id);
typedef void (*AiGenerateHook)(const char *prompt, const char *lang, const char *tone,
                              const char *format, const char *target);
typedef void (*AiConfigHook)(const char *provider, const char *model, const char *key,
                            const char *base);
typedef void (*DeleteCallback)(long long id);

/* Lazy glyph provider. Fills rows [offset, offset+limit) for the given tab.
   An empty/NULL query means "the whole tab"; otherwise the query is a
   localized search string. The provider must call super_v_ui_add_glyph_item()
   for each emitted row and return how many it emitted. */
typedef int (*GlyphProvider)(const char *tab, const char *query, int offset, int limit);

/* Skin-tone variants for one glyph: exact RGI sequences separated by 0x1F,
   written into caller-provided storage. Returns bytes written (excluding the
   NUL), or -1 when the glyph has no variants. The UI never composes a variant
   itself; multi-person sequences need two modifiers in the right slots. */
typedef int (*GlyphVariantsProvider)(const char *glyph, char *out, int cap);

/* Runs once, only in the primary instance, after the window is built. */
typedef void (*ActivateHook)(void);

/* Preferences -> behaviour bridges. */
typedef void (*LanguageHook)(const char *langs);   /* e.g. "en,id" */
typedef void (*GifProviderHook)(const char *id);   /* "local", "tenor", ... */
typedef void (*GifReloadHook)(void);
typedef void (*GifSearchHook)(const char *query);
typedef void (*GifMoreHook)(void);

/* Queue a callback on the GTK main loop; used by worker threads that fetched
   data and must touch widgets only from the main thread. */
typedef void (*MainTaskFn)(void *data);
void super_v_ui_post_to_main(MainTaskFn fn, void *data);
typedef void (*ApiKeyHook)(const char *key);
typedef void (*ProviderTestHook)(const char *provider);
typedef void (*PlacementHook)(const char *mode);
typedef void (*ClipTextHook)(const char *text);
typedef void (*ImageHook)(const char *path, const char *mime);

void super_v_ui_set_paste_callback(PasteCallback cb);
void super_v_ui_set_pin_callback(PinCallback cb);
void super_v_ui_set_refresh_hook(RefreshHook cb);
void super_v_ui_set_live_update_hook(UpdateHook cb);
void super_v_ui_set_snippet_hooks(SnippetAddHook add, SnippetDeleteHook del);
void super_v_ui_set_ai_generate_hook(AiGenerateHook cb);
void super_v_ui_set_ai_config_hook(AiConfigHook cb);
void super_v_ui_set_ai_config_values(const char *provider, const char *model,
                                     const char *key, const char *base);
void super_v_ui_add_snippet_row(const char *name, const char *body, long long id);
void super_v_ui_clear_snippet_rows(void);
void super_v_ui_set_ai_result(const char *text);
void super_v_ui_set_ai_status(const char *text);
void super_v_ui_set_delete_callback(DeleteCallback cb);
void super_v_ui_set_glyph_provider(GlyphProvider cb);
void super_v_ui_set_glyph_variants_provider(GlyphVariantsProvider cb);
void super_v_ui_set_activate_hook(ActivateHook cb);
void super_v_ui_set_language_hook(LanguageHook cb);
void super_v_ui_set_gif_provider_hook(GifProviderHook cb);
void super_v_ui_set_gif_reload_hook(GifReloadHook cb);
void super_v_ui_set_gif_search_hook(GifSearchHook cb);
void super_v_ui_set_gif_more_hook(GifMoreHook cb);
void super_v_ui_set_api_key_hook(ApiKeyHook cb);
void super_v_ui_set_provider_test_hook(ProviderTestHook cb);
void super_v_ui_set_placement_hook(PlacementHook cb);
void super_v_ui_set_placement(const char *mode);
void super_v_ui_set_clipboard_hook(ClipTextHook cb);
void super_v_ui_set_image_hook(ImageHook cb);
void super_v_ui_insert_text(const char *text);
void super_v_ui_paste_text(const char *text);

/* Called from Rust after the hooks are installed, so the C side can reflect
   the persisted preferences in its widgets. */
void super_v_ui_apply_preferences(const char *langs, const char *gif_provider);
void super_v_ui_set_api_key_text(const char *key);
void super_v_ui_report_provider_test(const char *message);
void super_v_ui_set_log_path(const char *path);
void super_v_ui_show_debug(void);

int super_v_ui_run_app(int argc, char **argv);
int super_v_ui_test_prewarm(char *version_buf, int max_len);
void super_v_ui_show_settings(void *parent_window);
void super_v_ui_show_about(void *parent_window);

void super_v_ui_add_history_item(const char *title, const char *subtitle, int is_pinned, int is_sensitive, long long id, const char *preview_image_path, long long created_at, const char *mime);
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

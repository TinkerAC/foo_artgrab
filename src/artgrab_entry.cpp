#include "stdafx.h"
#include "artgrab_entry.h"
#include "artgrab_gallery.h"
#include "async_io_manager.h"

typedef const char* (*pfn_get_key)();
typedef bool (*pfn_is_api_enabled)(const char*);
typedef void (*pfn_cache_remove)(const char*, const char*);
typedef void (*pfn_refresh)();

static pfn_get_key s_get_lastfm_key = nullptr;
static pfn_get_key s_get_discogs_key = nullptr;
static pfn_get_key s_get_discogs_consumer_key = nullptr;
static pfn_get_key s_get_discogs_consumer_secret = nullptr;
static pfn_is_api_enabled s_is_api_enabled = nullptr;
static pfn_cache_remove s_cache_remove = nullptr;
static pfn_refresh s_refresh = nullptr;
static ULONG_PTR s_gdiplus_token = 0;

void artgrab::initialize() {
    // GDI+ init
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&s_gdiplus_token, &input, nullptr);

    // Resolve foo_artwork key getters
    HMODULE hArtwork = GetModuleHandle(L"foo_artwork.dll");
    if (hArtwork) {
        s_get_lastfm_key = (pfn_get_key)GetProcAddress(hArtwork, "foo_artwork_get_lastfm_key");
        s_get_discogs_key = (pfn_get_key)GetProcAddress(hArtwork, "foo_artwork_get_discogs_key");
        s_get_discogs_consumer_key = (pfn_get_key)GetProcAddress(hArtwork, "foo_artwork_get_discogs_consumer_key");
        s_get_discogs_consumer_secret = (pfn_get_key)GetProcAddress(hArtwork, "foo_artwork_get_discogs_consumer_secret");
        s_is_api_enabled = (pfn_is_api_enabled)GetProcAddress(hArtwork, "foo_artwork_is_api_enabled");
        s_cache_remove = (pfn_cache_remove)GetProcAddress(hArtwork, "foo_artwork_cache_remove");
        s_refresh = (pfn_refresh)GetProcAddress(hArtwork, "foo_artwork_refresh");
    }

    // Async I/O init
    async_io_manager::instance().initialize(4);
}

void artgrab::shutdown() {
    async_io_manager::instance().shutdown();
    if (s_gdiplus_token) {
        Gdiplus::GdiplusShutdown(s_gdiplus_token);
        s_gdiplus_token = 0;
    }
}

pfc::string8 artgrab::get_effective_lastfm_key() {
    if (s_get_lastfm_key) {
        const char* key = s_get_lastfm_key();
        if (key && key[0]) return pfc::string8(key);
    }
    return pfc::string8();
}

pfc::string8 artgrab::get_effective_discogs_key() {
    if (s_get_discogs_key) {
        const char* key = s_get_discogs_key();
        if (key && key[0]) return pfc::string8(key);
    }
    return pfc::string8();
}

pfc::string8 artgrab::get_effective_discogs_consumer_key() {
    if (s_get_discogs_consumer_key) {
        const char* key = s_get_discogs_consumer_key();
        if (key && key[0]) return pfc::string8(key);
    }
    return pfc::string8();
}

pfc::string8 artgrab::get_effective_discogs_consumer_secret() {
    if (s_get_discogs_consumer_secret) {
        const char* key = s_get_discogs_consumer_secret();
        if (key && key[0]) return pfc::string8(key);
    }
    return pfc::string8();
}

void artgrab::invalidate_artwork_cache(const char* artist, const char* track) {
    if (s_cache_remove) s_cache_remove(artist, track);
}

void artgrab::refresh_artwork_panel() {
    if (s_refresh) s_refresh();
}

bool artgrab::is_api_enabled(const char* api_name) {
    if (s_is_api_enabled) return s_is_api_enabled(api_name);
    return true;  // If foo_artwork doesn't export the function, enable all by default
}

// initquit lifecycle
class artgrab_initquit : public initquit {
public:
    void on_init() override { artgrab::initialize(); }
    void on_quit() override { artgrab::shutdown(); }
};
static initquit_factory_t<artgrab_initquit> g_artgrab_initquit;

// The exported C function (retained for backwards compatibility with foo_artwork panel integration)
extern "C" __declspec(dllexport)
void foo_artgrab_open(const char* artist, const char* album, const char* file_path) {
    if (!artist || !album) return;
    // Recover the title for legacy foo_artwork callers from the now-playing item.
    // Updated callers should use foo_artgrab_open_v2 and pass it explicitly.
    pfc::string8 title;
    metadb_handle_ptr now_playing;
    static_api_ptr_t<playback_control> playback;
    if (playback->get_now_playing(now_playing) &&
        (!file_path || !file_path[0] || strcmp(now_playing->get_path(), file_path) == 0)) {
        static_api_ptr_t<titleformat_compiler> compiler;
        titleformat_object::ptr tf_title;
        compiler->compile_safe_ex(tf_title, "%title%");
        now_playing->format_title(nullptr, title, tf_title, nullptr);
    }
    auto* gallery = new artgrab::GalleryWindow(
        artist, album, title.get_ptr(), file_path ? file_path : "", now_playing);
    gallery->Show(core_api::get_main_window());
}

extern "C" __declspec(dllexport)
void foo_artgrab_open_v2(const char* artist, const char* album, const char* title, const char* file_path) {
    if (!artist || !album || !title) return;
    metadb_handle_ptr target;
    metadb_handle_ptr now_playing;
    static_api_ptr_t<playback_control> playback;
    if (playback->get_now_playing(now_playing) &&
        (!file_path || !file_path[0] || strcmp(now_playing->get_path(), file_path) == 0)) {
        target = now_playing;
    }
    auto* gallery = new artgrab::GalleryWindow(
        artist, album, title, file_path ? file_path : "", target);
    gallery->Show(core_api::get_main_window());
}


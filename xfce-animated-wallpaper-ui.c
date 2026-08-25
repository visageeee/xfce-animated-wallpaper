#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gtk/gtkx.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>


typedef struct {
    gchar *id, *name, *placeholder;
    gdouble min, max, step, default_value;
    gint digits, order;
    GtkWidget *slider;
    GtkWidget *audio_badge;
} EffectParam;

typedef struct {
    gchar *id, *name, *description, *shader_path, *icon_path;
    gint order;
    GPtrArray *params;
} EffectDef;

typedef struct {
    GtkWidget *window;
    GtkWidget *file_button;
    GtkWidget *wallpaper_row;
    GtkWidget *stream_row;
    GtkWidget *source_local;
    GtkWidget *source_stream;
    GtkWidget *stream_entry;
    GtkWidget *reconnect_check;
    GtkWidget *preview_stack;
    GtkWidget *preview_eventbox;
    GtkWidget *preview_area;
    GtkWidget *preview_label;
    GtkWidget *preview_audio_label;
    GtkWidget *preview_toggle_button;
    GtkWidget *notebook;
    GtkWidget *set_wallpaper_button;
    GtkWidget *speed_scale;
    GtkWidget *mode_fill;
    GtkWidget *mode_fit;
    GtkWidget *mode_stretch;
    GtkWidget *mute_check;
    GtkWidget *loop_check;
    GtkWidget *hwdec_check;
    GtkWidget *fps_spin;
    GtkWidget *autostart_check;
    GtkWidget *desktop_icons_check;
    GtkWidget *audio_enabled_check;
    GtkWidget *audio_waveform_check;
    GtkWidget *audio_effect_label;
    GtkWidget *audio_parameter_combo;
    GtkWidget *audio_source_combo;
    GtkWidget *audio_device_combo;
    GtkWidget *audio_sensitivity_scale;
    GtkWidget *audio_smoothing_scale;
    GtkWidget *preset_list;
    GtkWidget *preset_values_view;
    GtkWidget *preset_load_button;
    GtkWidget *preset_delete_button;
    GtkWidget *interpolation_check;
    GtkWidget *pause_fullscreen_check;
    GtkWidget *pause_battery_check;
    GtkWidget *brightness_scale;
    GtkWidget *contrast_scale;
    GtkWidget *saturation_scale;
    GtkWidget *status_label;
    GtkWidget *status_indicator;
    GtkWidget *turn_off_button;
    gboolean status_active;
    gboolean settings_dirty;
    gboolean changing_effects;
    GPtrArray *effects;
    guint status_poll_source;
    gboolean enabled;
    gboolean loading;
    gchar *applied_video;
    gchar *applied_stream;
    gchar *applied_source;
    GPid preview_pid;
    GPid preview_audio_pid;
    gchar *preview_ipc_path;
    guint preview_child_watch_source;
    guint preview_restart_source;
    guint preview_aspect_source;
    gboolean preview_restart_pending;
    gboolean preview_enabled;
    GtkWidget *fullscreen_window;
    GtkWidget *fullscreen_socket;
    GPid fullscreen_pid;
    guint fullscreen_child_watch_source;
    guint fullscreen_start_source;
    GPid fullscreen_audio_pid;
    gchar *fullscreen_ipc_path;
    gboolean fullscreen_paused_preview;
    gboolean fullscreen_paused_preview_audio;
    gboolean fullscreen_paused_wallpaper;
    gboolean fullscreen_paused_wallpaper_audio;
} App;


static gboolean path_is_static_image(const gchar *path) {
    if (!path || !*path)
        return FALSE;

    gchar *lower = g_ascii_strdown(path, -1);
    gboolean image =
        g_str_has_suffix(lower, ".png")  ||
        g_str_has_suffix(lower, ".jpg")  ||
        g_str_has_suffix(lower, ".jpeg") ||
        g_str_has_suffix(lower, ".webp") ||
        g_str_has_suffix(lower, ".bmp")  ||
        g_str_has_suffix(lower, ".tif")  ||
        g_str_has_suffix(lower, ".tiff");

    g_free(lower);
    return image;
}





static gchar *static_image_cache_video(const gchar *image_path, GError **error) {
    if (!image_path || !*image_path)
        return NULL;

    GStatBuf st;
    if (g_stat(image_path, &st) != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Could not stat image: %s", image_path);
        return NULL;
    }

    gchar *key_src = g_strdup_printf("%s|%" G_GINT64_FORMAT "|%" G_GINT64_FORMAT,
                                     image_path,
                                     (gint64)st.st_mtime,
                                     (gint64)st.st_size);
    gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, key_src, -1);
    g_free(key_src);

    gchar *cache_dir = g_build_filename(g_get_user_cache_dir(),
                                        "xfce-animated-wallpaper",
                                        "static-videos", NULL);
    g_mkdir_with_parents(cache_dir, 0700);

    gchar *name = g_strdup_printf("%s.mp4", hash);
    gchar *out = g_build_filename(cache_dir, name, NULL);

    g_free(name);
    g_free(hash);

    if (g_file_test(out, G_FILE_TEST_IS_REGULAR)) {
        g_free(cache_dir);
        return out;
    }

    gchar *tmp = g_strdup_printf("%s.tmp.mp4", out);
    gchar *quoted_in = g_shell_quote(image_path);
    gchar *quoted_out = g_shell_quote(tmp);

    /*
     * Build one second of 30 FPS H.264 from the still image. The result is
     * tiny, hardware-decodable on typical GPUs, and loops like any normal
     * wallpaper video. yuv420p maximizes decoder compatibility.
     */
    gchar *cmd = g_strdup_printf(
        "ffmpeg -hide_banner -loglevel error -y "
        "-loop 1 -framerate 30 -i %s "
        "-t 1 -an -vf \"fps=30,pad=ceil(iw/2)*2:ceil(ih/2)*2,format=yuv420p\" "
        "-c:v libx264 -preset veryfast -crf 18 "
        "-g 30 -keyint_min 30 -movflags +faststart %s",
        quoted_in, quoted_out);

    gint status = 0;
    gchar *stderr_text = NULL;
    gboolean ok = g_spawn_command_line_sync(cmd, NULL, &stderr_text, &status, error);

    g_free(cmd);
    g_free(quoted_in);
    g_free(quoted_out);

    if (!ok || status != 0) {
        if (error && !*error) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                        "ffmpeg could not prepare static image%s%s",
                        stderr_text && *stderr_text ? ": " : "",
                        stderr_text && *stderr_text ? stderr_text : "");
        }
        g_free(stderr_text);
        g_unlink(tmp);
        g_free(tmp);
        g_free(out);
        g_free(cache_dir);
        return NULL;
    }

    g_free(stderr_text);

    if (g_rename(tmp, out) != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Could not finalize cached static wallpaper video.");
        g_unlink(tmp);
        g_free(tmp);
        g_free(out);
        g_free(cache_dir);
        return NULL;
    }

    g_free(tmp);
    g_free(cache_dir);
    return out;
}

static gboolean source_is_stream(App *app);
static void on_setting_changed(GtkWidget *widget, gpointer data);
static EffectDef *active_effect(App *app);
static void update_audio_control_indicators(App *app);
static void stop_preview_audio(App *app);
static void stop_fullscreen_showcase(App *app);
static void start_fullscreen_showcase(App *app);

static void effect_param_free(gpointer data) {
    EffectParam *p = data;
    if (!p) return;
    g_free(p->id); g_free(p->name); g_free(p->placeholder); g_free(p);
}
static void effect_def_free(gpointer data) {
    EffectDef *e = data;
    if (!e) return;
    g_free(e->id); g_free(e->name); g_free(e->description);
    g_free(e->shader_path); g_free(e->icon_path);
    if (e->params) g_ptr_array_free(e->params, TRUE);
    g_free(e);
}
static gint effect_sort(gconstpointer a, gconstpointer b) {
    const EffectDef *ea = *(EffectDef * const *)a;
    const EffectDef *eb = *(EffectDef * const *)b;
    return g_utf8_collate(ea->name ? ea->name : "",
                          eb->name ? eb->name : "");
}
static gint effect_param_sort(gconstpointer a, gconstpointer b) {
    const EffectParam *pa = *(EffectParam * const *)a, *pb = *(EffectParam * const *)b;
    if (pa->order != pb->order) return pa->order - pb->order;
    return g_strcmp0(pa->name, pb->name);
}
/* Effect IDs become config group names and generated shader filenames.
 * Keep them deliberately boring so a third-party effect cannot escape those
 * namespaces with path separators or other metacharacters. */
static gboolean effect_id_is_safe(const gchar *id) {
    if (!id || !*id)
        return FALSE;

    for (const guchar *p = (const guchar *)id; *p; p++) {
        if (!(g_ascii_isalnum(*p) || *p == '_' || *p == '-'))
            return FALSE;
    }

    return TRUE;
}

/* Manifest asset names must name a file inside the effect directory, not an
 * absolute path or a path containing ../ (or subdirectories). */
static gboolean effect_asset_name_is_safe(const gchar *name) {
    if (!name || !*name)
        return FALSE;

    return g_path_is_absolute(name) == FALSE &&
           strchr(name, '/') == NULL &&
           strchr(name, '\\') == NULL &&
           g_strcmp0(name, ".") != 0 &&
           g_strcmp0(name, "..") != 0;
}

static gboolean effect_id_loaded(GPtrArray *effects, const gchar *id) {
    for (guint i=0; effects && i<effects->len; i++) {
        EffectDef *e = g_ptr_array_index(effects,i);
        if (g_strcmp0(e->id,id)==0) return TRUE;
    }
    return FALSE;
}
static EffectParam *effect_activation_param(EffectDef *e) {
    if (!e || !e->params || !e->params->len) return NULL;
    for (guint i=0;i<e->params->len;i++) {
        EffectParam *p=g_ptr_array_index(e->params,i);
        if (g_strcmp0(p->id,"strength")==0) return p;
    }
    return g_ptr_array_index(e->params,0);
}
static EffectDef *effect_for_slider(App *app, GtkWidget *slider, EffectParam **out) {
    for (guint i=0; app->effects && i<app->effects->len; i++) {
        EffectDef *e=g_ptr_array_index(app->effects,i);
        for (guint j=0;j<e->params->len;j++) {
            EffectParam *p=g_ptr_array_index(e->params,j);
            if (p->slider==slider) { if(out)*out=p; return e; }
        }
    }
    if (out)
        *out = NULL;

    return NULL;
}
static void load_effect_parameters(GKeyFile *kf, EffectDef *e) {
    gsize n=0; gchar **groups=g_key_file_get_groups(kf,&n);
    for (gsize i=0; groups && i<n; i++) {
        if (!g_str_has_prefix(groups[i],"Parameter ")) continue;
        const gchar *id=groups[i]+strlen("Parameter ");
        if(!*id) continue;
        EffectParam *p=g_new0(EffectParam,1);
        p->id=g_strdup(id);
        p->name=g_key_file_has_key(kf,groups[i],"name",NULL)?g_key_file_get_string(kf,groups[i],"name",NULL):g_strdup(id);
        p->placeholder=g_key_file_has_key(kf,groups[i],"placeholder",NULL)?g_key_file_get_string(kf,groups[i],"placeholder",NULL):g_ascii_strup(id,-1);
        p->min=g_key_file_has_key(kf,groups[i],"min",NULL)?g_key_file_get_double(kf,groups[i],"min",NULL):0;
        p->max=g_key_file_has_key(kf,groups[i],"max",NULL)?g_key_file_get_double(kf,groups[i],"max",NULL):100;
        p->step=g_key_file_has_key(kf,groups[i],"step",NULL)?g_key_file_get_double(kf,groups[i],"step",NULL):1;
        p->default_value=g_key_file_has_key(kf,groups[i],"default",NULL)?g_key_file_get_double(kf,groups[i],"default",NULL):p->min;
        p->digits=g_key_file_has_key(kf,groups[i],"digits",NULL)?g_key_file_get_integer(kf,groups[i],"digits",NULL):0;
        p->order=g_key_file_has_key(kf,groups[i],"order",NULL)?g_key_file_get_integer(kf,groups[i],"order",NULL):1000;
        g_ptr_array_add(e->params,p);
    }
    g_strfreev(groups);
    if (!e->params->len) {
        EffectParam *p=g_new0(EffectParam,1);
        p->id=g_strdup("strength"); p->name=g_strdup("Strength"); p->placeholder=g_strdup("VALUE");
        p->min=g_key_file_has_key(kf,"Effect","min",NULL)?g_key_file_get_double(kf,"Effect","min",NULL):0;
        p->max=g_key_file_has_key(kf,"Effect","max",NULL)?g_key_file_get_double(kf,"Effect","max",NULL):100;
        p->step=g_key_file_has_key(kf,"Effect","step",NULL)?g_key_file_get_double(kf,"Effect","step",NULL):1;
        p->default_value=g_key_file_has_key(kf,"Effect","default",NULL)?g_key_file_get_double(kf,"Effect","default",NULL):p->min;
        p->digits=g_key_file_has_key(kf,"Effect","digits",NULL)?g_key_file_get_integer(kf,"Effect","digits",NULL):0;
        g_ptr_array_add(e->params,p);
    }
    g_ptr_array_sort(e->params,effect_param_sort);
}
static void load_effect_dir(GPtrArray *effects,const gchar *base) {
    if(!base||!g_file_test(base,G_FILE_TEST_IS_DIR))return;
    GDir *dir=g_dir_open(base,0,NULL); if(!dir)return;
    const gchar *entry;
    while((entry=g_dir_read_name(dir))) {
        gchar *folder=g_build_filename(base,entry,NULL);
        gchar *manifest=g_build_filename(folder,"effect.ini",NULL);
        if(!g_file_test(manifest,G_FILE_TEST_IS_REGULAR)){g_free(manifest);g_free(folder);continue;}
        GKeyFile *kf=g_key_file_new();
        if(!g_key_file_load_from_file(kf,manifest,G_KEY_FILE_NONE,NULL)){g_key_file_unref(kf);g_free(manifest);g_free(folder);continue;}
        gchar *id=g_key_file_get_string(kf,"Effect","id",NULL);
        gchar *name=g_key_file_get_string(kf,"Effect","name",NULL);
        gchar *desc=g_key_file_get_string(kf,"Effect","description",NULL);
        gchar *shader_name=g_key_file_get_string(kf,"Effect","shader",NULL);
        gchar *icon_name=g_key_file_has_key(kf,"Effect","icon",NULL)
                           ? g_key_file_get_string(kf,"Effect","icon",NULL)
                           : NULL;
        if(!effect_id_is_safe(id) || !name || !*name ||
           !effect_asset_name_is_safe(shader_name) ||
           (icon_name && *icon_name && !effect_asset_name_is_safe(icon_name)) ||
           effect_id_loaded(effects,id)){
            g_free(id);g_free(name);g_free(desc);g_free(shader_name);g_free(icon_name);g_key_file_unref(kf);g_free(manifest);g_free(folder);continue;
        }
        gchar *shader=g_build_filename(folder,shader_name,NULL);
        if(!g_file_test(shader,G_FILE_TEST_IS_REGULAR)){
            g_free(shader);g_free(id);g_free(name);g_free(desc);g_free(shader_name);g_free(icon_name);g_key_file_unref(kf);g_free(manifest);g_free(folder);continue;
        }
        EffectDef *e=g_new0(EffectDef,1);
        e->id=id;e->name=name;e->description=desc?desc:g_strdup("");e->shader_path=shader;

        if (icon_name && *icon_name) {
            gchar *candidate = g_build_filename(folder, icon_name, NULL);
            if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
                e->icon_path = candidate;
            else
                g_free(candidate);
        }

        e->order=g_key_file_has_key(kf,"Effect","order",NULL)?g_key_file_get_integer(kf,"Effect","order",NULL):1000;
        e->params=g_ptr_array_new_with_free_func(effect_param_free);
        load_effect_parameters(kf,e); g_ptr_array_add(effects,e);
        g_free(icon_name);g_free(shader_name);g_key_file_unref(kf);g_free(manifest);g_free(folder);
    }
    g_dir_close(dir);
}
static GPtrArray *discover_effects(void) {
    GPtrArray *effects=g_ptr_array_new_with_free_func(effect_def_free);
    gchar *user=g_build_filename(g_get_user_data_dir(),"xfce-animated-wallpaper","effects",NULL);
    load_effect_dir(effects,user);g_free(user);
    load_effect_dir(effects,"./effects");
    load_effect_dir(effects,"/usr/local/share/xfce-animated-wallpaper/effects");
    load_effect_dir(effects,"/usr/share/xfce-animated-wallpaper/effects");
    g_ptr_array_sort(effects,effect_sort); return effects;
}

static gchar *config_dir(void) {
    return g_build_filename(g_get_user_config_dir(), "xfce-animated-wallpaper", NULL);
}

static gchar *config_path(void) {
    return g_build_filename(g_get_user_config_dir(), "xfce-animated-wallpaper", "config.ini", NULL);
}

static gchar *autostart_path(void) {
    return g_build_filename(g_get_user_config_dir(), "autostart", "xfce-animated-wallpaper.desktop", NULL);
}

static gboolean command_sync(const gchar *action, gchar **stdout_text) {
    gchar *argv[] = {"xfce-animated-wallpaper", (gchar *)action, NULL};
    GError *err = NULL;
    gint status = 0;
    gchar *stderr_text = NULL;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, stdout_text, &stderr_text, &status, &err);
    if (!ok) {
        g_warning("%s", err->message);
        g_clear_error(&err);
        g_free(stderr_text);
        return FALSE;
    }
    if (!g_spawn_check_wait_status(status, NULL) && stderr_text && *stderr_text)
        g_warning("%s", stderr_text);
    g_free(stderr_text);
    return g_spawn_check_wait_status(status, NULL);
}

static gboolean draw_status_indicator(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    App *app = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    gdouble r = MIN(a.width, a.height) * 0.34;
    cairo_arc(cr, a.width / 2.0, a.height / 2.0, r, 0, 2 * G_PI);
    if (app->status_active && app->settings_dirty)
        cairo_set_source_rgb(cr, 0.92, 0.68, 0.12);
    else if (app->status_active)
        cairo_set_source_rgb(cr, 0.20, 0.72, 0.30);
    else
        cairo_set_source_rgb(cr, 0.82, 0.20, 0.20);
    cairo_fill(cr);
    return FALSE;
}


static gchar *wallpaper_log_path(void) {
    return g_build_filename(g_get_user_cache_dir(),
                            "xfce-animated-wallpaper",
                            "wallpaper-mpv.log", NULL);
}

static gchar *friendly_stream_error_from_text(const gchar *text) {
    if (!text || !*text)
        return NULL;

    if (strstr(text, "HTTP error 403") || strstr(text, "HTTP Error 403") ||
        strstr(text, "403 Forbidden"))
        return g_strdup("YouTube or the stream provider refused the video stream (HTTP 403).");

    if (strstr(text, "yt-dlp") &&
        (strstr(text, "ERROR:") || strstr(text, "error")))
        return g_strdup("yt-dlp could not resolve or open this web video.");

    if (strstr(text, "Failed to open") || strstr(text, "Errors when loading file") ||
        strstr(text, "No video or audio streams selected"))
        return g_strdup("The stream could not be opened.");

    if (strstr(text, "Connection refused") || strstr(text, "timed out") ||
        strstr(text, "Network is unreachable"))
        return g_strdup("The stream could not be reached.");

    return NULL;
}

static gchar *read_wallpaper_error(void) {
    gchar *path = wallpaper_log_path();
    gchar *text = NULL;
    g_file_get_contents(path, &text, NULL, NULL);
    g_free(path);

    gchar *friendly = friendly_stream_error_from_text(text);
    g_free(text);
    return friendly;
}

static void update_status(App *app) {
    gchar *out = NULL;
    app->status_active = command_sync("status", &out) && out && g_str_has_prefix(out, "running");

    if (app->status_active && app->settings_dirty) {
        gtk_label_set_text(GTK_LABEL(app->status_label),
                           "Press \"Set Wallpaper\" to apply settings");
    } else if (app->status_active) {
        const gchar *kind =
            (app->applied_source && g_strcmp0(app->applied_source, "stream") == 0)
                ? "Web source"
                : "Local file";
        gchar *status = g_strdup_printf("Animated wallpaper is active - %s", kind);
        gtk_label_set_text(GTK_LABEL(app->status_label), status);
        g_free(status);
    } else if (app->enabled) {
        gchar *friendly = read_wallpaper_error();
        if (friendly) {
            gtk_label_set_text(GTK_LABEL(app->status_label), friendly);
        } else {
            const gchar *kind =
                (app->applied_source && g_strcmp0(app->applied_source, "stream") == 0)
                    ? "web source"
                    : "local file";
            gchar *status = g_strdup_printf("Animated wallpaper stopped unexpectedly (%s)", kind);
            gtk_label_set_text(GTK_LABEL(app->status_label), status);
            g_free(status);
        }
        g_free(friendly);
    } else {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Using Xfce desktop background");
    }

    if (app->status_indicator) gtk_widget_queue_draw(app->status_indicator);
    if (app->turn_off_button) gtk_widget_set_sensitive(app->turn_off_button, app->status_active);
    g_free(out);
}

static gboolean status_poll_cb(gpointer data) {
    update_status((App *)data);
    return G_SOURCE_CONTINUE;
}

static void save_config(App *app) {
    if (app->loading) return;

    GKeyFile *kf = g_key_file_new();
    gchar *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(app->file_button));
    const gchar *stream_url = gtk_entry_get_text(GTK_ENTRY(app->stream_entry));
    const gchar *source_key =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->source_stream))
            ? "stream" : "local";
    const gchar *mode_key = "fill";
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->mode_fit))) mode_key = "fit";
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->mode_stretch))) mode_key = "stretch";

    g_key_file_set_string(kf, "wallpaper", "source", source_key);
    g_key_file_set_string(kf, "wallpaper", "video", file ? file : "");
    g_key_file_set_string(kf, "wallpaper", "stream_url", stream_url ? stream_url : "");
    g_key_file_set_boolean(kf, "wallpaper", "reconnect",
                           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->reconnect_check)));

    g_key_file_set_string(kf, "wallpaper", "applied_source",
                          app->applied_source ? app->applied_source : "");
    g_key_file_set_string(kf, "wallpaper", "applied_video",
                          app->applied_video ? app->applied_video : "");
    g_key_file_set_string(kf, "wallpaper", "applied_stream",
                          app->applied_stream ? app->applied_stream : "");
    g_key_file_set_string(kf, "wallpaper", "mode", mode_key);
    g_key_file_set_boolean(kf, "wallpaper", "enabled", app->enabled);
    g_key_file_set_double(kf, "playback", "speed", gtk_range_get_value(GTK_RANGE(app->speed_scale)));
    g_key_file_set_boolean(kf, "playback", "mute", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->mute_check)));
    g_key_file_set_boolean(kf, "playback", "loop", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->loop_check)));
    g_key_file_set_boolean(kf, "playback", "hwdec", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->hwdec_check)));
    g_key_file_set_boolean(kf, "desktop", "show_icons", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->desktop_icons_check)));
    g_key_file_set_boolean(kf, "audio", "enabled", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->audio_enabled_check)));
    g_key_file_set_boolean(kf, "audio", "show_waveform", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->audio_waveform_check)));
    const gchar *audio_param = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->audio_parameter_combo));
    g_key_file_set_string(kf, "audio", "parameter", audio_param ? audio_param : "strength");
    gchar *audio_source_text =
        gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->audio_source_combo));
    g_key_file_set_string(kf, "audio", "source",
                          audio_source_text &&
                          g_strcmp0(audio_source_text, "Overall level") == 0
                              ? "overall" : "bass");
    g_free(audio_source_text);
    const gchar *audio_device_id =
        gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->audio_device_combo));
    g_key_file_set_string(kf, "audio", "device",
                          audio_device_id ? audio_device_id : "automatic");
    g_key_file_set_double(kf, "audio", "sensitivity", gtk_range_get_value(GTK_RANGE(app->audio_sensitivity_scale)));
    g_key_file_set_double(kf, "audio", "smoothing", gtk_range_get_value(GTK_RANGE(app->audio_smoothing_scale)));
    g_key_file_set_integer(kf, "playback", "fps_limit", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->fps_spin)));
    g_key_file_set_boolean(kf, "advanced", "interpolation", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->interpolation_check)));
    g_key_file_set_boolean(kf, "advanced", "pause_fullscreen", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->pause_fullscreen_check)));
    g_key_file_set_boolean(kf, "advanced", "pause_battery", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->pause_battery_check)));
    g_key_file_set_double(kf, "advanced", "brightness", gtk_range_get_value(GTK_RANGE(app->brightness_scale)));
    g_key_file_set_double(kf, "advanced", "contrast", gtk_range_get_value(GTK_RANGE(app->contrast_scale)));
    g_key_file_set_double(kf, "advanced", "saturation", gtk_range_get_value(GTK_RANGE(app->saturation_scale)));

    /*
     * Store the active effect explicitly.  The individual values remain for
     * compatibility and preset parameter storage, but the backend no longer
     * has to guess which module is active by scanning slider values.
     */
    EffectDef *selected_effect = active_effect(app);
    g_key_file_set_string(kf, "effects", "active",
                          selected_effect ? selected_effect->id : "");

    for (guint i=0; app->effects && i<app->effects->len; i++) {
        EffectDef *e=g_ptr_array_index(app->effects,i);
        gchar *group=g_strdup_printf("effect.%s",e->id);
        for(guint j=0;j<e->params->len;j++){
            EffectParam *p=g_ptr_array_index(e->params,j);
            g_key_file_set_double(kf,group,p->id,gtk_range_get_value(GTK_RANGE(p->slider)));
        }
        EffectParam *a=effect_activation_param(e);
        if(a)g_key_file_set_double(kf,"effects",e->id,gtk_range_get_value(GTK_RANGE(a->slider)));
        g_free(group);
    }

    gchar *dir = config_dir();
    g_mkdir_with_parents(dir, 0700);
    gchar *path = config_path();
    gchar *data = g_key_file_to_data(kf, NULL, NULL);
    g_file_set_contents(path, data, -1, NULL);

    g_free(data);
    g_free(path);
    g_free(dir);
    g_free(file);
    g_key_file_unref(kf);
}


static void reset_defaults(App *app) {
    app->loading = TRUE;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->mode_fill), TRUE);
    gtk_range_set_value(GTK_RANGE(app->speed_scale), 1.0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->mute_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->loop_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hwdec_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->desktop_icons_check), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->audio_enabled_check), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->audio_waveform_check), FALSE);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->audio_source_combo), 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->audio_device_combo), 0);
    gtk_range_set_value(GTK_RANGE(app->audio_sensitivity_scale), 2.0);
    gtk_range_set_value(GTK_RANGE(app->audio_smoothing_scale), 0.82);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->fps_spin), 0);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->interpolation_check), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->pause_fullscreen_check), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->pause_battery_check), FALSE);
    gtk_range_set_value(GTK_RANGE(app->brightness_scale), 0.0);
    gtk_range_set_value(GTK_RANGE(app->contrast_scale), 0.0);
    gtk_range_set_value(GTK_RANGE(app->saturation_scale), 0.0);
    for(guint i=0;app->effects&&i<app->effects->len;i++){
        EffectDef *e=g_ptr_array_index(app->effects,i);
        for(guint j=0;j<e->params->len;j++){EffectParam *p=g_ptr_array_index(e->params,j);gtk_range_set_value(GTK_RANGE(p->slider),p->default_value);}
    }

    app->loading = FALSE;
    app->settings_dirty = TRUE;
    if (app->status_indicator) gtk_widget_queue_draw(app->status_indicator);
    update_status(app);
    save_config(app);
}

static void on_reset_clicked(GtkButton *button, gpointer data) {
    (void)button;
    reset_defaults((App *)data);
}

static void set_autostart(gboolean enabled) {
    gchar *path = autostart_path();
    gchar *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);

    if (enabled) {
        const gchar *desktop =
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Animated Wallpaper\n"
            "Comment=Restore the selected animated wallpaper\n"
            "Exec=sh -c 'sleep 5; exec xfce-animated-wallpaper autostart'\n"
            "Terminal=false\n"
            "OnlyShowIn=XFCE;\n"
            "X-GNOME-Autostart-enabled=true\n";
        g_file_set_contents(path, desktop, -1, NULL);
    } else {
        g_unlink(path);
    }

    g_free(dir);
    g_free(path);
}

static gboolean restart_preview_cb(gpointer data);
static void force_preview_source_refresh(App *app);

static void preview_child_setup(gpointer data) {
    (void)data;
    /*
     * Put the shell adapter and the mpv it execs into their own process
     * group.  That lets every stop/restart kill the entire preview tree
     * without touching wallpaper mpv instances.
     */
    setpgid(0, 0);
}


static void preview_child_exited(GPid pid, gint status, gpointer data) {
    App *app = data;
    stop_preview_audio(app);

    app->preview_child_watch_source = 0;

    if (app->preview_pid == pid)
        app->preview_pid = 0;

    g_spawn_close_pid(pid);

    if (app->preview_enabled &&
        app->preview_restart_pending &&
        app->preview_restart_source == 0) {
        app->preview_restart_source =
            g_timeout_add(120, restart_preview_cb, app);
        return;
    }

    if (!app->preview_restart_pending) {
        gchar *cache_dir = g_build_filename(
            g_get_user_cache_dir(), "xfce-animated-wallpaper", NULL);
        gchar *log_path =
            g_build_filename(cache_dir, "preview-mpv.log", NULL);
        gchar *log_text = NULL;

        g_file_get_contents(log_path, &log_text, NULL, NULL);
        gchar *friendly = friendly_stream_error_from_text(log_text);

        if (friendly) {
            gtk_label_set_text(GTK_LABEL(app->preview_label), friendly);
            gtk_stack_set_visible_child_name(
                GTK_STACK(app->preview_stack), "message");
        }

        g_free(friendly);
        g_free(log_text);
        g_free(log_path);
        g_free(cache_dir);
    }

    (void)status;
}

static gchar *preview_audio_ipc_path(void) {
    const gchar *runtime = g_get_user_runtime_dir();

    return g_build_filename(
        runtime ? runtime : "/tmp",
        "xfce-animated-wallpaper-preview-mpv.sock",
        NULL);
}

static void stop_preview_audio(App *app) {
    if (app->preview_audio_pid > 1) {
        kill(app->preview_audio_pid, SIGTERM);
        g_usleep(50000);
        g_spawn_close_pid(app->preview_audio_pid);
        app->preview_audio_pid = 0;
    }

    if (app->preview_ipc_path) {
        g_unlink(app->preview_ipc_path);
        g_free(app->preview_ipc_path);
        app->preview_ipc_path = NULL;
    }
}

static gboolean start_preview_audio(App *app) {
    stop_preview_audio(app);

    app->preview_ipc_path = preview_audio_ipc_path();
    g_unlink(app->preview_ipc_path);

    gchar *argv[] = {
        (gchar *)"xfce-animated-wallpaper-visualizer",
        (gchar *)"--control",
        (gchar *)"--no-waveform",
        (gchar *)"--ipc-path",
        app->preview_ipc_path,
        NULL
    };

    GError *error = NULL;
    GPid pid = 0;

    gboolean ok = g_spawn_async(
        NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH |
        G_SPAWN_DO_NOT_REAP_CHILD |
        G_SPAWN_STDOUT_TO_DEV_NULL |
        G_SPAWN_STDERR_TO_DEV_NULL,
        NULL,
        NULL,
        &pid,
        &error);

    if (!ok) {
        g_clear_error(&error);
        stop_preview_audio(app);
        return FALSE;
    }

    app->preview_audio_pid = pid;
    return TRUE;
}

static void stop_preview(App *app) {
    stop_preview_audio(app);

    GPid pid = app->preview_pid;
    if (pid <= 1)
        return;

    /*
     * We deliberately remove the GLib child watch before synchronously
     * reaping this child.  Otherwise both stop_preview() and the watch could
     * try to own the same child lifecycle.
     */
    if (app->preview_child_watch_source) {
        g_source_remove(app->preview_child_watch_source);
        app->preview_child_watch_source = 0;
    }

    /* The preview is its own process group (PGID == child PID). */
    kill(-pid, SIGTERM);

    gint status = 0;
    gboolean reaped = FALSE;

    for (int i = 0; i < 20; i++) {
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid) {
            reaped = TRUE;
            break;
        }

        if (result < 0) {
            reaped = TRUE; /* already gone/reaped */
            break;
        }

        g_usleep(25000);
    }

    if (!reaped) {
        kill(-pid, SIGKILL);

        /* After SIGKILL, wait for the direct child so no zombie is left. */
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
    }

    g_spawn_close_pid(pid);
    app->preview_pid = 0;
}

static void show_preview_message(App *app, const gchar *message) {
    stop_preview(app);
    gtk_label_set_text(GTK_LABEL(app->preview_label), message);
    gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "message");
}


static gboolean source_is_stream(App *app) {
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->source_stream));
}

static gchar *selected_source(App *app) {
    if (source_is_stream(app)) {
        const gchar *url = gtk_entry_get_text(GTK_ENTRY(app->stream_entry));
        return g_strdup(url ? url : "");
    }
    return gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(app->file_button));
}

static gboolean source_is_valid(App *app, const gchar *source) {
    if (!source || !*source)
        return FALSE;
    if (source_is_stream(app))
        return strstr(source, "://") != NULL;
    return g_file_test(source, G_FILE_TEST_IS_REGULAR);
}


static gboolean url_is_direct_stream(const gchar *url) {
    if (!url || !*url) return FALSE;
    gchar *lower = g_ascii_strdown(url, -1);
    gboolean direct =
        g_str_has_prefix(lower, "rtsp://") ||
        g_str_has_prefix(lower, "rtmp://") ||
        g_str_has_prefix(lower, "udp://") ||
        g_str_has_prefix(lower, "tcp://") ||
        strstr(lower, ".m3u8") != NULL ||
        strstr(lower, ".mpd") != NULL ||
        g_str_has_suffix(lower, ".mp4") ||
        g_str_has_suffix(lower, ".webm") ||
        g_str_has_suffix(lower, ".mkv") ||
        g_str_has_suffix(lower, ".mov") ||
        g_str_has_suffix(lower, ".ts");
    g_free(lower);
    return direct;
}

static void update_source_controls(App *app) {
    gboolean stream = source_is_stream(app);

    if (app->wallpaper_row)
        gtk_widget_set_visible(app->wallpaper_row, !stream);
    if (app->stream_row)
        gtk_widget_set_visible(app->stream_row, stream);

    gtk_widget_set_sensitive(app->file_button, !stream);
    gtk_widget_set_sensitive(app->stream_entry, stream);
    gtk_widget_set_visible(app->reconnect_check, stream);
    gtk_widget_set_sensitive(app->loop_check, !stream);
}



static void set_preview_aspect(App *app, gdouble aspect) {
    if (aspect <= 0.01)
        aspect = 16.0 / 9.0;

    /*
     * Keep the preview width fixed.  Error/message labels must never be able
     * to widen the preview column; monitor aspect only changes its height.
     */
    const gint width = 240;
    gint height = (gint)(width / aspect + 0.5);

    /* Avoid absurd heights on portrait/extreme displays while preserving the
     * fixed-width behavior. */
    height = CLAMP(height, 80, 240);

    gtk_widget_set_size_request(app->preview_stack, width, height);
    gtk_widget_set_size_request(app->preview_area, width, height);
}

static gdouble current_monitor_aspect(App *app) {
    if (!app->window)
        return 16.0 / 9.0;

    GdkWindow *window = gtk_widget_get_window(app->window);
    if (!window)
        return 16.0 / 9.0;

    GdkDisplay *display = gdk_window_get_display(window);
    if (!display)
        return 16.0 / 9.0;

    GdkMonitor *monitor = gdk_display_get_monitor_at_window(display, window);
    if (!monitor)
        return 16.0 / 9.0;

    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);

    if (geometry.width <= 0 || geometry.height <= 0)
        return 16.0 / 9.0;

    return (gdouble)geometry.width / (gdouble)geometry.height;
}

static gboolean update_preview_aspect_cb(gpointer data) {
    App *app = data;
    app->preview_aspect_source = 0;
    set_preview_aspect(app, current_monitor_aspect(app));
    return G_SOURCE_REMOVE;
}

static void schedule_preview_aspect_update(App *app) {
    if (app->preview_aspect_source)
        g_source_remove(app->preview_aspect_source);
    app->preview_aspect_source = g_timeout_add(60, update_preview_aspect_cb, app);
}

static gboolean on_window_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    (void)widget;
    (void)event;
    schedule_preview_aspect_update((App *)data);
    return FALSE;
}



static gchar *materialize_effect_shader(App *app, EffectDef *effect) {
    gchar *rendered = NULL;
    if (!effect ||
        !g_file_get_contents(effect->shader_path, &rendered, NULL, NULL))
        return NULL;

    gboolean audio_enabled =
        app->audio_enabled_check &&
        gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->audio_enabled_check));

    const gchar *audio_parameter =
        app->audio_parameter_combo
            ? gtk_combo_box_get_active_id(
                  GTK_COMBO_BOX(app->audio_parameter_combo))
            : NULL;

    gboolean uses_audio = FALSE;

    for (guint i = 0; i < effect->params->len; i++) {
        EffectParam *p = g_ptr_array_index(effect->params, i);

        gdouble base =
            gtk_range_get_value(GTK_RANGE(p->slider));

        gchar *replacement = NULL;

        if (audio_enabled &&
            audio_parameter &&
            g_strcmp0(p->id, audio_parameter) == 0) {
            gchar min_value[G_ASCII_DTOSTR_BUF_SIZE];
            gchar base_value[G_ASCII_DTOSTR_BUF_SIZE];

            g_ascii_formatd(
                min_value, sizeof min_value, "%.6f", p->min);
            g_ascii_formatd(
                base_value, sizeof base_value, "%.6f", base);

            replacement = g_strdup_printf(
                "mix(%s, %s, aw_audio)",
                min_value, base_value);
            uses_audio = TRUE;
        } else {
            gchar value[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(
                value, sizeof value, "%.6f", base);
            replacement = g_strdup(value);
        }

        gchar *token =
            g_strdup_printf("@%s@", p->placeholder);
        gchar **parts =
            g_strsplit(rendered, token, -1);
        gchar *next =
            g_strjoinv(replacement, parts);

        g_strfreev(parts);
        g_free(token);
        g_free(replacement);
        g_free(rendered);
        rendered = next;
    }

    if (uses_audio) {
        const gchar *param_block =
            "//!PARAM aw_audio\n"
            "//!DESC Live audio level\n"
            "//!TYPE DYNAMIC float\n"
            "//!MINIMUM 0.0\n"
            "//!MAXIMUM 1.0\n"
            "0.0\n\n";

        gchar *next =
            g_strconcat(param_block, rendered, NULL);
        g_free(rendered);
        rendered = next;
    }

    gchar *dir =
        g_build_filename(
            g_get_user_cache_dir(),
            "xfce-animated-wallpaper",
            "shaders",
            NULL);
    g_mkdir_with_parents(dir, 0700);

    gchar *name =
        g_strdup_printf("%s-generated.glsl", effect->id);
    gchar *path =
        g_build_filename(dir, name, NULL);

    if (!g_file_set_contents(path, rendered, -1, NULL)) {
        g_free(path);
        path = NULL;
    }

    g_free(name);
    g_free(dir);
    g_free(rendered);
    return path;
}

static void add_effect_shader(App *app,
                              GPtrArray *argv,
                              EffectDef *effect) {
    gchar *path =
        materialize_effect_shader(app, effect);

    if (!path)
        return;

    g_ptr_array_add(
        argv,
        g_strdup_printf("--glsl-shader=%s", path));
    g_free(path);
}

static EffectDef *active_effect(App *app){
    for(guint i=0;app->effects&&i<app->effects->len;i++){
        EffectDef *e=g_ptr_array_index(app->effects,i);EffectParam *p=effect_activation_param(e);
        if(p&&gtk_range_get_value(GTK_RANGE(p->slider))>p->min+0.001)return e;
    }return NULL;
}


static EffectParam *audio_controlled_param(App *app, EffectDef **effect_out) {
    if (effect_out)
        *effect_out = NULL;

    if (!app->audio_enabled_check ||
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->audio_enabled_check)))
        return NULL;

    EffectDef *effect = active_effect(app);
    if (!effect || !app->audio_parameter_combo)
        return NULL;

    const gchar *parameter_id =
        gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->audio_parameter_combo));
    if (!parameter_id || !*parameter_id)
        return NULL;

    for (guint i = 0; i < effect->params->len; i++) {
        EffectParam *p = g_ptr_array_index(effect->params, i);
        if (g_strcmp0(p->id, parameter_id) == 0) {
            if (effect_out)
                *effect_out = effect;
            return p;
        }
    }

    return NULL;
}

static void update_audio_control_indicators(App *app) {
    EffectDef *controlled_effect = NULL;
    EffectParam *controlled =
        audio_controlled_param(app, &controlled_effect);

    for (guint i = 0; app->effects && i < app->effects->len; i++) {
        EffectDef *e = g_ptr_array_index(app->effects, i);

        for (guint j = 0; j < e->params->len; j++) {
            EffectParam *p = g_ptr_array_index(e->params, j);

            if (p->audio_badge) {
                gtk_widget_set_visible(
                    p->audio_badge,
                    e == controlled_effect && p == controlled);
            }
        }
    }

    if (!app->preview_audio_label)
        return;

    if (controlled_effect && controlled) {
        gchar *text = g_strdup_printf(
            "Audio controlled:\n%s - %s",
            controlled_effect->name,
            controlled->name);
        gtk_label_set_text(GTK_LABEL(app->preview_audio_label), text);
        gtk_widget_show(app->preview_audio_label);
        g_free(text);
    } else {
        gtk_widget_hide(app->preview_audio_label);
    }
}


static void update_preview(App *app) {
    gchar *cache_dir_debug = g_build_filename(g_get_user_cache_dir(), "xfce-animated-wallpaper", NULL);
    g_mkdir_with_parents(cache_dir_debug, 0700);
    gchar *debug_path = g_build_filename(cache_dir_debug, "preview-debug.log", NULL);
    gchar *debug_line = g_strdup_printf("update_preview: realized=%d mapped=%d\n",
                                        gtk_widget_get_realized(app->preview_area),
                                        gtk_widget_get_mapped(app->preview_area));
    g_file_set_contents(debug_path, debug_line, -1, NULL);
    g_free(debug_line);
    g_free(debug_path);
    g_free(cache_dir_debug);

    gchar *video = selected_source(app);
    if (!source_is_valid(app, video)) {
        show_preview_message(app,
            source_is_stream(app)
                ? "Enter a stream URL to preview it"
                : "Click the preview to choose wallpaper media");
        g_free(video);
        return;
    }

    if (!gtk_widget_get_realized(app->preview_area)) {
        g_free(video);
        return;
    }

    GdkDisplay *display = gtk_widget_get_display(app->preview_area);
    if (!display || !GDK_IS_X11_DISPLAY(display)) {
        show_preview_message(app, "Animated preview requires X11");
        g_free(video);
        return;
    }

    gchar *mpv = g_find_program_in_path("mpv");
    if (!mpv) {
        show_preview_message(app, "Install mpv to show the animated preview");
        g_free(video);
        return;
    }

    Window xid = gtk_socket_get_id(GTK_SOCKET(app->preview_area));
    {
        gchar *cache_dir2 = g_build_filename(g_get_user_cache_dir(), "xfce-animated-wallpaper", NULL);
        g_mkdir_with_parents(cache_dir2, 0700);
        gchar *debug_path2 = g_build_filename(cache_dir2, "preview-debug.log", NULL);
        gchar *line2 = g_strdup_printf("preview target: xid=0x%lx (%lu) video=%s\n",
                                       (unsigned long)xid, (unsigned long)xid, video);
        FILE *fp2 = fopen(debug_path2, "a");
        if (fp2) { fputs(line2, fp2); fclose(fp2); }
        g_free(line2); g_free(debug_path2); g_free(cache_dir2);
    }
    if (xid == 0) {
        show_preview_message(app, "Could not create the preview window");
        g_free(mpv);
        g_free(video);
        return;
    }
    gchar *xid_arg = g_strdup_printf("%lu", (unsigned long)xid);

    /* mpv command-line numbers must always use a dot as the decimal
     * separator.  GTK initializes the user's locale, so ordinary printf
     * formatting would produce e.g. 1,0 under sv_SE and mpv rejects it. */
    gchar speed_num[G_ASCII_DTOSTR_BUF_SIZE];
    gchar brightness_num[G_ASCII_DTOSTR_BUF_SIZE];
    gchar contrast_num[G_ASCII_DTOSTR_BUF_SIZE];
    gchar saturation_num[G_ASCII_DTOSTR_BUF_SIZE];

    g_ascii_formatd(speed_num, sizeof speed_num, "%.1f",
                    gtk_range_get_value(GTK_RANGE(app->speed_scale)));
    g_ascii_formatd(brightness_num, sizeof brightness_num, "%.0f",
                    gtk_range_get_value(GTK_RANGE(app->brightness_scale)));
    g_ascii_formatd(contrast_num, sizeof contrast_num, "%.0f",
                    gtk_range_get_value(GTK_RANGE(app->contrast_scale)));
    g_ascii_formatd(saturation_num, sizeof saturation_num, "%.0f",
                    gtk_range_get_value(GTK_RANGE(app->saturation_scale)));

    gchar *speed_arg = g_strdup_printf("--speed=%s", speed_num);
    gchar *brightness_arg = g_strdup_printf("--brightness=%s", brightness_num);
    gchar *contrast_arg = g_strdup_printf("--contrast=%s", contrast_num);
    gchar *saturation_arg = g_strdup_printf("--saturation=%s", saturation_num);


    gchar *cache_dir = g_build_filename(g_get_user_cache_dir(), "xfce-animated-wallpaper", NULL);
    g_mkdir_with_parents(cache_dir, 0700);
    gchar *log_path = g_build_filename(cache_dir, "preview-mpv.log", NULL);

    /*
     * Use the same shell adapter pattern as the wallpaper backend.  This keeps
     * the XID as a plain positional parameter and lets the shell construct the
     * --wid=<id> form required by current mpv versions.  stdout/stderr go to a
     * persistent log so preview failures are diagnosable.
     */
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup("sh"));
    g_ptr_array_add(argv, g_strdup("-c"));
    g_ptr_array_add(argv, g_strdup(
        "log=$1; wid=$2; shift 2; "
        ": >\"$log\"; "
        "exec mpv --wid=\"$wid\" \"$@\" >>\"$log\" 2>&1"));
    g_ptr_array_add(argv, g_strdup("sh"));
    g_ptr_array_add(argv, g_strdup(log_path));
    g_ptr_array_add(argv, xid_arg);
    gchar *preview_media = NULL;
    gchar *preview_static_cache = NULL;

    if (!source_is_stream(app) && path_is_static_image(video)) {
        GError *cache_err = NULL;
        preview_static_cache = static_image_cache_video(video, &cache_err);
        if (preview_static_cache) {
            g_ptr_array_add(argv, g_strdup("--loop-file=inf"));
            preview_media = g_strdup(preview_static_cache);
        } else {
            gchar *msg = g_strdup_printf("Could not prepare static image: %s",
                                         cache_err ? cache_err->message : "unknown error");
            gtk_label_set_text(GTK_LABEL(app->preview_label), msg);
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "message");
            g_free(msg);
            g_clear_error(&cache_err);
            g_ptr_array_free(argv, TRUE);
            g_free(log_path);
            g_free(video);
            return;
        }
    } else {
        if (!source_is_stream(app) &&
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->loop_check)))
            g_ptr_array_add(argv, g_strdup("--loop-file=inf"));
        preview_media = g_strdup(video);
    }

    if (source_is_stream(app)) {
        if (url_is_direct_stream(video)) {
            /* Let mpv/FFmpeg handle direct stream continuity itself. */
            if (!g_str_has_prefix(video, "rtsp://"))
                g_ptr_array_add(argv, g_strdup("--network-timeout=15"));
        } else {
            /* Webpage URLs such as YouTube use mpv's yt-dlp hook. */
            g_ptr_array_add(argv, g_strdup("--ytdl=yes"));
            g_ptr_array_add(argv, g_strdup("--script-opts=ytdl_hook-try_ytdl_first=yes"));
        }
    }

    g_ptr_array_add(argv, g_strdup("--no-audio"));
    g_ptr_array_add(argv, g_strdup("--vo=gpu-next"));
    g_ptr_array_add(argv, g_strdup("--no-osc"));
    g_ptr_array_add(argv, g_strdup("--no-input-default-bindings"));
    g_ptr_array_add(argv, g_strdup("--input-cursor-passthrough=yes"));
    g_ptr_array_add(argv, g_strdup("--msg-level=all=info"));
    g_ptr_array_add(argv, g_strdup("--force-window=yes"));
    g_ptr_array_add(argv, speed_arg);
    g_ptr_array_add(argv, brightness_arg);
    g_ptr_array_add(argv, contrast_arg);
    g_ptr_array_add(argv, saturation_arg);

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->interpolation_check))) {
        g_ptr_array_add(argv, g_strdup("--interpolation=yes"));
        g_ptr_array_add(argv, g_strdup("--video-sync=display-resample"));
    }

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->hwdec_check)))
        g_ptr_array_add(argv, g_strdup("--hwdec=auto-safe"));
    else
        g_ptr_array_add(argv, g_strdup("--hwdec=no"));

    gboolean preview_audio_enabled =
        app->audio_enabled_check &&
        gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->audio_enabled_check));

    if (preview_audio_enabled) {
        stop_preview_audio(app);
        app->preview_ipc_path = preview_audio_ipc_path();
        g_unlink(app->preview_ipc_path);

        g_ptr_array_add(
            argv,
            g_strdup_printf(
                "--input-ipc-server=%s",
                app->preview_ipc_path));
    }

    /* Built-in effects are GPU shaders and do not require software decoding. */
    EffectDef *effect = active_effect(app);
    if (effect)
        add_effect_shader(app, argv, effect);

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->mode_stretch))) {
        g_ptr_array_add(argv, g_strdup("--keepaspect=no"));
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->mode_fit))) {
        g_ptr_array_add(argv, g_strdup("--keepaspect=yes"));
        g_ptr_array_add(argv, g_strdup("--panscan=0.0"));
    } else {
        g_ptr_array_add(argv, g_strdup("--keepaspect=yes"));
        g_ptr_array_add(argv, g_strdup("--panscan=1.0"));
    }

    g_ptr_array_add(argv, preview_media);
    g_ptr_array_add(argv, NULL);

    GError *err = NULL;
    GPid pid = 0;
    gboolean ok = g_spawn_async(NULL, (gchar **)argv->pdata, NULL,
                                G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                preview_child_setup, NULL, &pid, &err);
    if (ok) {
        app->preview_pid = pid;
        app->preview_child_watch_source =
            g_child_watch_add(pid, preview_child_exited, app);

        if (preview_audio_enabled)
            start_preview_audio(app);

        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "video");
        gchar *cache_dir3 = g_build_filename(g_get_user_cache_dir(), "xfce-animated-wallpaper", NULL);
        gchar *debug_path3 = g_build_filename(cache_dir3, "preview-debug.log", NULL);
        gchar *line3 = g_strdup_printf("preview spawn ok: pid=%ld\n", (long)pid);
        FILE *fp3 = fopen(debug_path3, "a");
        if (fp3) { fputs(line3, fp3); fclose(fp3); }
        g_free(line3); g_free(debug_path3); g_free(cache_dir3);
    } else {
        gchar *msg = g_strdup_printf("Could not start preview: %s", err ? err->message : "unknown error");
        gtk_label_set_text(GTK_LABEL(app->preview_label), msg);
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "message");
        g_free(msg);
        g_clear_error(&err);
    }

    g_ptr_array_free(argv, TRUE);
    g_free(preview_static_cache);
    g_free(log_path);
    g_free(cache_dir);
    g_free(mpv);
    g_free(video);
}

static gboolean restart_preview_cb(gpointer data) {
    App *app = data;
    app->preview_restart_source = 0;

    if (!app->preview_enabled) {
        app->preview_restart_pending = FALSE;
        return G_SOURCE_REMOVE;
    }

    if (app->preview_pid > 0)
        return G_SOURCE_REMOVE;

    app->preview_restart_pending = FALSE;
    update_preview(app);
    return G_SOURCE_REMOVE;
}

static void schedule_preview_restart(App *app) {
    schedule_preview_aspect_update(app);

    if (!app->preview_enabled) {
        app->preview_restart_pending = FALSE;
        return;
    }

    app->preview_restart_pending = TRUE;

    if (app->preview_restart_source) {
        g_source_remove(app->preview_restart_source);
        app->preview_restart_source = 0;
    }

    /*
     * Fully tear down and reap the old preview first.  There can therefore
     * never be two preview mpv process groups alive at the same time.
     */
    stop_preview(app);

    app->preview_restart_source =
        g_timeout_add(120, restart_preview_cb, app);
}

static gboolean on_preview_plug_removed(GtkSocket *socket, gpointer data) {
    (void)socket;
    App *app = data;

    if (app->preview_enabled &&
        app->preview_restart_pending &&
        app->preview_pid == 0 &&
        app->preview_restart_source == 0) {
        app->preview_restart_source =
            g_timeout_add(120, restart_preview_cb, app);
    }

    return TRUE;
}

static void on_preview_realize(GtkWidget *widget, gpointer data) {
    (void)widget;
    schedule_preview_restart((App *)data);
}


static void on_preview_toggle_clicked(GtkButton *button, gpointer data) {
    App *app = data;

    if (app->preview_enabled) {
        app->preview_enabled = FALSE;
        app->preview_restart_pending = FALSE;

        if (app->preview_restart_source) {
            g_source_remove(app->preview_restart_source);
            app->preview_restart_source = 0;
        }

        stop_preview(app);
        gtk_label_set_text(GTK_LABEL(app->preview_label), "Preview is off");
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "message");
        gtk_button_set_label(button, "Turn on preview");
        return;
    }

    app->preview_enabled = TRUE;
    gtk_button_set_label(button, "Turn off preview");

    /*
     * Re-enable through the same restart state machine used for ordinary
     * preview updates.  There is no source cache: update_preview() always
     * reads the current file chooser / URL when the restart occurs.
     */
    schedule_preview_restart(app);
}

static gboolean on_window_focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)widget; (void)event;
    App *app = data;
    if (app->preview_pid > 0) kill(app->preview_pid, SIGCONT);
    return FALSE;
}

static gboolean on_window_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)widget; (void)event;
    App *app = data;
    if (app->preview_pid > 0) kill(app->preview_pid, SIGSTOP);
    return FALSE;
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    App *app = data;
    stop_fullscreen_showcase(app);
    (void)widget;
    stop_preview((App *)data);
    g_clear_pointer(&app->applied_video, g_free);
    g_clear_pointer(&app->applied_stream, g_free);
    g_clear_pointer(&app->applied_source, g_free);
    if (app->effects) {
        g_ptr_array_free(app->effects, TRUE);
        app->effects = NULL;
    }
    gtk_main_quit();
}


typedef struct {
    App *app;
    GtkWidget *dialog;
    gchar *filename;
} GalleryChoice;

static gboolean supported_wallpaper_file(const gchar *name) {
    if (!name) return FALSE;
    gchar *lower = g_ascii_strdown(name, -1);
    const gchar *exts[] = {
        ".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v", ".mpg", ".mpeg",
        ".gif", ".apng",
        ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff", NULL
    };
    gboolean ok = FALSE;
    for (int i = 0; exts[i]; i++) {
        if (g_str_has_suffix(lower, exts[i])) { ok = TRUE; break; }
    }
    g_free(lower);
    return ok;
}

static gchar *gallery_thumbnail_for(const gchar *filename) {
    gchar *ffmpeg = g_find_program_in_path("ffmpeg");
    if (!ffmpeg) return NULL;

    gchar *sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, filename, -1);
    gchar *dir = g_build_filename(g_get_user_cache_dir(), "xfce-animated-wallpaper", "gallery", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *base = g_strdup_printf("%s.jpg", sum);
    gchar *thumb = g_build_filename(dir, base, NULL);

    if (!g_file_test(thumb, G_FILE_TEST_IS_REGULAR)) {
        gchar *argv[] = {
            ffmpeg,
            "-hide_banner", "-loglevel", "error", "-y",
            "-ss", "1",
            "-i", (gchar *)filename,
            "-frames:v", "1",
            "-vf", "scale=180:100:force_original_aspect_ratio=decrease,pad=180:100:(ow-iw)/2:(oh-ih)/2",
            thumb,
            NULL
        };
        gint status = 0;
        GError *err = NULL;
        gboolean ok = g_spawn_sync(NULL, argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &err);
        if (!ok || !g_spawn_check_wait_status(status, NULL)) {
            if (err) g_clear_error(&err);
            argv[5] = "0";
            ok = g_spawn_sync(NULL, argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &err);
        }
        if (!ok || !g_spawn_check_wait_status(status, NULL)) {
            g_unlink(thumb);
        }
        if (err) g_clear_error(&err);
    }

    g_free(ffmpeg);
    g_free(sum);
    g_free(dir);
    g_free(base);
    if (!g_file_test(thumb, G_FILE_TEST_IS_REGULAR)) {
        g_free(thumb);
        return NULL;
    }
    return thumb;
}

static void gallery_choice_free(gpointer data, GClosure *closure) {
    (void)closure;
    GalleryChoice *choice = data;
    g_free(choice->filename);
    g_free(choice);
}

static void on_gallery_item_clicked(GtkButton *button, gpointer data) {
    (void)button;
    GalleryChoice *choice = data;
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(choice->app->file_button), choice->filename);
    save_config(choice->app);
    schedule_preview_restart(choice->app);
    gtk_widget_destroy(choice->dialog);
}

static GtkWidget *gallery_tile(App *app, GtkWidget *dialog, const gchar *filename) {
    GtkWidget *button = gtk_button_new();
    gtk_widget_set_size_request(button, 200, 145);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);
    gtk_container_add(GTK_CONTAINER(button), box);

    gchar *thumb = gallery_thumbnail_for(filename);
    GtkWidget *image = NULL;
    if (thumb) {
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(thumb, 180, 100, TRUE, NULL);
        image = pixbuf ? gtk_image_new_from_pixbuf(pixbuf) : gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
        if (pixbuf) g_object_unref(pixbuf);
    } else {
        image = gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
    }
    gtk_box_pack_start(GTK_BOX(box), image, TRUE, TRUE, 0);

    gchar *base = g_path_get_basename(filename);
    GtkWidget *label = gtk_label_new(base);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(button, filename);

    GalleryChoice *choice = g_new0(GalleryChoice, 1);
    choice->app = app;
    choice->dialog = dialog;
    choice->filename = g_strdup(filename);
    g_signal_connect_data(button, "clicked", G_CALLBACK(on_gallery_item_clicked), choice,
                          gallery_choice_free, 0);

    g_free(thumb);
    g_free(base);
    return button;
}

static gint compare_string_ptrs(gconstpointer a, gconstpointer b) {
    const gchar *sa = *(const gchar * const *)a;
    const gchar *sb = *(const gchar * const *)b;
    return g_utf8_collate(sa, sb);
}

static void populate_gallery(App *app, GtkWidget *dialog, GtkWidget *flow, const gchar *folder) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(flow));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir) return;

    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!supported_wallpaper_file(name)) continue;
        gchar *path = g_build_filename(folder, name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            g_ptr_array_add(files, path);
        else
            g_free(path);
    }
    g_dir_close(dir);
    g_ptr_array_sort(files, compare_string_ptrs);

    guint limit = MIN(files->len, 100);
    for (guint i = 0; i < limit; i++) {
        GtkWidget *tile = gallery_tile(app, dialog, g_ptr_array_index(files, i));
        gtk_container_add(GTK_CONTAINER(flow), tile);
    }

    if (files->len == 0) {
        GtkWidget *label = gtk_label_new("No supported animated wallpapers found in this folder.");
        gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");
        gtk_container_add(GTK_CONTAINER(flow), label);
    }
    g_ptr_array_free(files, TRUE);
    gtk_widget_show_all(flow);
}

typedef struct {
    App *app;
    GtkWidget *dialog;
    GtkWidget *flow;
    GtkWidget *folder_label;
    gchar *folder;
} GalleryWindow;

static void gallery_window_free(gpointer data, GClosure *closure) {
    (void)closure;
    GalleryWindow *gw = data;
    g_free(gw->folder);
    g_free(gw);
}

static void on_gallery_change_folder(GtkButton *button, gpointer data) {
    (void)button;
    GalleryWindow *gw = data;
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        "Choose wallpaper folder", GTK_WINDOW(gw->dialog), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Cancel", GTK_RESPONSE_CANCEL, "Open", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), gw->folder);
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (folder) {
            g_free(gw->folder);
            gw->folder = folder;
            gtk_label_set_text(GTK_LABEL(gw->folder_label), gw->folder);
            populate_gallery(gw->app, gw->dialog, gw->flow, gw->folder);
        }
    }
    gtk_widget_destroy(chooser);
}

static void show_gallery(App *app) {
    gchar *current = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(app->file_button));
    gchar *folder = NULL;
    if (current && *current)
        folder = g_path_get_dirname(current);
    else
        folder = g_build_filename(g_get_home_dir(), "Pictures", NULL);
    g_free(current);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Wallpaper Gallery", GTK_WINDOW(app->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 560);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(content), top, FALSE, FALSE, 0);

    GtkWidget *folder_label = gtk_label_new(folder);
    gtk_label_set_xalign(GTK_LABEL(folder_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(folder_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(top), folder_label, TRUE, TRUE, 0);
    GtkWidget *change = gtk_button_new_with_label("Change Folderâ€¦");
    gtk_box_pack_end(GTK_BOX(top), change, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 10);
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 3);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 8);
    gtk_container_set_border_width(GTK_CONTAINER(flow), 4);
    gtk_container_add(GTK_CONTAINER(scroll), flow);

    GalleryWindow *gw = g_new0(GalleryWindow, 1);
    gw->app = app;
    gw->dialog = dialog;
    gw->flow = flow;
    gw->folder_label = folder_label;
    gw->folder = g_strdup(folder);
    g_signal_connect_data(change, "clicked", G_CALLBACK(on_gallery_change_folder), gw,
                          gallery_window_free, 0);

    populate_gallery(app, dialog, flow, folder);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(folder);
}

static void on_gallery_clicked(GtkButton *button, gpointer data) {
    (void)button;
    show_gallery((App *)data);
}

static gboolean on_preview_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    if (event->button != 1) return FALSE;
    App *app = data;

    if (source_is_stream(app)) {
        gtk_widget_grab_focus(app->stream_entry);
        gtk_editable_select_region(GTK_EDITABLE(app->stream_entry), 0, -1);
        return TRUE;
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Choose wallpaper media", GTK_WINDOW(app->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL, "Open", GTK_RESPONSE_ACCEPT, NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Wallpaper media (video and images)");
    gtk_file_filter_add_mime_type(filter, "video/*");
    gtk_file_filter_add_mime_type(filter, "image/*");
    gtk_file_filter_add_pattern(filter, "*.gif");
    gtk_file_filter_add_pattern(filter, "*.GIF");
    gtk_file_filter_add_pattern(filter, "*.apng");
    gtk_file_filter_add_pattern(filter, "*.APNG");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.PNG");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.JPG");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    gtk_file_filter_add_pattern(filter, "*.JPEG");
    gtk_file_filter_add_pattern(filter, "*.webp");
    gtk_file_filter_add_pattern(filter, "*.WEBP");
    gtk_file_filter_add_pattern(filter, "*.bmp");
    gtk_file_filter_add_pattern(filter, "*.BMP");
    gtk_file_filter_add_pattern(filter, "*.tif");
    gtk_file_filter_add_pattern(filter, "*.TIF");
    gtk_file_filter_add_pattern(filter, "*.tiff");
    gtk_file_filter_add_pattern(filter, "*.TIFF");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    gchar *current = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(app->file_button));
    if (current) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), current);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(app->file_button), filename);

            /*
             * Programmatically changing GtkFileChooserButton here does not
             * reliably emit "file-set", so explicitly use the same safe
             * preview restart path as the normal chooser/gallery.
             */
            app->settings_dirty = TRUE;
            if (app->status_indicator) gtk_widget_queue_draw(app->status_indicator);
            update_status(app);
            save_config(app);
            force_preview_source_refresh(app);

            g_free(filename);
        }
    }
    g_free(current);
    gtk_widget_destroy(dialog);
    return TRUE;
}


static void on_source_toggled(GtkToggleButton *button, gpointer data) {
    App *app = data;
    if (!gtk_toggle_button_get_active(button) || app->loading)
        return;
    app->settings_dirty = TRUE;
    if (app->status_indicator) gtk_widget_queue_draw(app->status_indicator);
    update_source_controls(app);
    update_status(app);
    save_config(app);
    force_preview_source_refresh(app);
}


static void refresh_audio_effect_controls(App *app) {
    if (!app->audio_effect_label || !app->audio_parameter_combo)
        return;

    gchar *previous = NULL;
    const gchar *active_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->audio_parameter_combo));
    if (active_id) previous = g_strdup(active_id);

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->audio_parameter_combo));
    EffectDef *effect = active_effect(app);

    if (!effect) {
        gtk_label_set_text(GTK_LABEL(app->audio_effect_label), "Active effect: None");
        gtk_widget_set_sensitive(app->audio_parameter_combo, FALSE);
        g_free(previous);
        update_audio_control_indicators(app);
        return;
    }

    gchar *label = g_strdup_printf("Active effect: %s", effect->name);
    gtk_label_set_text(GTK_LABEL(app->audio_effect_label), label);
    g_free(label);
    gtk_widget_set_sensitive(app->audio_parameter_combo, TRUE);

    for (guint i = 0; i < effect->params->len; i++) {
        EffectParam *p = g_ptr_array_index(effect->params, i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->audio_parameter_combo),
                                  p->id, p->name);
    }

    if (previous && gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->audio_parameter_combo), previous)) {
        /* preserved */
    } else if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->audio_parameter_combo), "strength")) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->audio_parameter_combo), 0);
    }
    g_free(previous);
    update_audio_control_indicators(app);
}

static void zero_other_effects(App *app,EffectDef *active){
    for(guint i=0;app->effects&&i<app->effects->len;i++){EffectDef *e=g_ptr_array_index(app->effects,i);if(e==active)continue;EffectParam *p=effect_activation_param(e);if(p&&gtk_range_get_value(GTK_RANGE(p->slider))>p->min+0.001)gtk_range_set_value(GTK_RANGE(p->slider),p->min);}
}

static void on_effect_changed(GtkRange *range,gpointer data){
    App *app=data;if(app->loading||app->changing_effects)return;
    EffectParam *p=NULL;EffectDef *e=effect_for_slider(app,GTK_WIDGET(range),&p);EffectParam *a=effect_activation_param(e);
    if(e&&p==a&&gtk_range_get_value(range)>a->min+0.001){app->changing_effects=TRUE;zero_other_effects(app,e);app->changing_effects=FALSE;}
    refresh_audio_effect_controls(app);
    on_setting_changed(GTK_WIDGET(range),app);
}

static void on_setting_changed(GtkWidget *widget, gpointer data) {
    (void)widget;
    App *app = data;
    if (app->loading) return;
    app->settings_dirty = TRUE;
    if (app->status_indicator) gtk_widget_queue_draw(app->status_indicator);
    update_status(app);
    update_audio_control_indicators(app);
    save_config(app);
    schedule_preview_restart(app);
}

static void force_preview_source_refresh(App *app) {
    schedule_preview_restart(app);
}

static void on_file_set(GtkFileChooserButton *button, gpointer data) {
    (void)button;
    App *app = data;
    if (app->loading)
        return;

    app->settings_dirty = TRUE;
    if (app->status_indicator)
        gtk_widget_queue_draw(app->status_indicator);

    update_status(app);
    save_config(app);
    force_preview_source_refresh(app);
}

static void on_autostart_toggled(GtkToggleButton *button, gpointer data) {
    (void)data;
    set_autostart(gtk_toggle_button_get_active(button));
}

static void on_set_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    gchar *video = selected_source(app);
    if (!source_is_valid(app, video)) {
        gtk_label_set_text(GTK_LABEL(app->status_label),
                           source_is_stream(app)
                               ? "Enter a valid stream URL first"
                               : "Choose a valid animated wallpaper first");
        g_free(video);
        return;
    }

    g_free(app->applied_source);
    g_free(app->applied_video);
    g_free(app->applied_stream);

    if (source_is_stream(app)) {
        app->applied_source = g_strdup("stream");
        app->applied_stream = g_strdup(video);
        app->applied_video = NULL;
    } else {
        app->applied_source = g_strdup("local");
        app->applied_video = g_strdup(video);
        app->applied_stream = NULL;
    }
    g_free(video);

    app->enabled = TRUE;
    save_config(app);
    if (!command_sync("restart", NULL)) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Could not start animated wallpaper");
        return;
    }
    app->settings_dirty = FALSE;
    if (app->status_indicator) gtk_widget_queue_draw(app->status_indicator);
    update_status(app);
}

static void on_turn_off_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    app->enabled = FALSE;
    app->settings_dirty = FALSE;
    save_config(app);
    command_sync("stop", NULL);
    if (app->status_indicator) gtk_widget_queue_draw(app->status_indicator);
    update_status(app);
}

static void load_config(App *app) {
    app->loading = TRUE;

    gchar *path = config_path();
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    gboolean loaded = g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err);
    if (err) g_clear_error(&err);

    gchar *video = loaded ? g_key_file_get_string(kf, "wallpaper", "video", NULL) : NULL;
    gchar *stream_url = loaded && g_key_file_has_key(kf, "wallpaper", "stream_url", NULL)
                          ? g_key_file_get_string(kf, "wallpaper", "stream_url", NULL) : NULL;
    gchar *source = loaded && g_key_file_has_key(kf, "wallpaper", "source", NULL)
                      ? g_key_file_get_string(kf, "wallpaper", "source", NULL) : g_strdup("local");
    gboolean reconnect = loaded && g_key_file_has_key(kf, "wallpaper", "reconnect", NULL)
                           ? g_key_file_get_boolean(kf, "wallpaper", "reconnect", NULL) : TRUE;

    app->applied_source = loaded && g_key_file_has_key(kf, "wallpaper", "applied_source", NULL)
                            ? g_key_file_get_string(kf, "wallpaper", "applied_source", NULL)
                            : NULL;
    app->applied_video = loaded && g_key_file_has_key(kf, "wallpaper", "applied_video", NULL)
                           ? g_key_file_get_string(kf, "wallpaper", "applied_video", NULL)
                           : NULL;
    app->applied_stream = loaded && g_key_file_has_key(kf, "wallpaper", "applied_stream", NULL)
                            ? g_key_file_get_string(kf, "wallpaper", "applied_stream", NULL)
                            : NULL;

    app->applied_video = loaded && g_key_file_has_key(kf, "wallpaper", "applied_video", NULL)
                           ? g_key_file_get_string(kf, "wallpaper", "applied_video", NULL)
                           : NULL;

    /* On launch, show the wallpaper actually applied with Set Wallpaper,
     * not the last file merely browsed in the preview UI. */
    if (app->applied_video && *app->applied_video &&
        g_file_test(app->applied_video, G_FILE_TEST_IS_REGULAR)) {
        g_free(video);
        video = g_strdup(app->applied_video);
    }

    /* On launch, show the source actually applied with Set Wallpaper. */
    if (app->applied_source && g_strcmp0(app->applied_source, "stream") == 0 &&
        app->applied_stream && *app->applied_stream) {
        g_free(source);
        source = g_strdup("stream");
        g_free(stream_url);
        stream_url = g_strdup(app->applied_stream);
    } else if (app->applied_video && *app->applied_video &&
               g_file_test(app->applied_video, G_FILE_TEST_IS_REGULAR)) {
        g_free(source);
        source = g_strdup("local");
        g_free(video);
        video = g_strdup(app->applied_video);
    }

    gchar *mode = loaded ? g_key_file_get_string(kf, "wallpaper", "mode", NULL) : NULL;
    app->enabled = loaded && g_key_file_get_boolean(kf, "wallpaper", "enabled", NULL);
    gdouble speed = loaded ? g_key_file_get_double(kf, "playback", "speed", NULL) : 1.0;
    gboolean mute = loaded ? g_key_file_get_boolean(kf, "playback", "mute", NULL) : TRUE;
    gboolean loop = loaded ? g_key_file_get_boolean(kf, "playback", "loop", NULL) : TRUE;
    gboolean hwdec = loaded ? g_key_file_get_boolean(kf, "playback", "hwdec", NULL) : TRUE;
    gboolean desktop_icons = loaded && g_key_file_has_key(kf, "desktop", "show_icons", NULL) ? g_key_file_get_boolean(kf, "desktop", "show_icons", NULL) : FALSE;
    gboolean audio_enabled = loaded && g_key_file_has_key(kf, "audio", "enabled", NULL) ? g_key_file_get_boolean(kf, "audio", "enabled", NULL) : FALSE;
    gboolean audio_waveform = loaded && g_key_file_has_key(kf, "audio", "show_waveform", NULL) ? g_key_file_get_boolean(kf, "audio", "show_waveform", NULL) : FALSE;
    gchar *audio_source = loaded && g_key_file_has_key(kf, "audio", "source", NULL) ? g_key_file_get_string(kf, "audio", "source", NULL) : g_strdup("bass");
    gchar *audio_device = loaded && g_key_file_has_key(kf, "audio", "device", NULL)
                            ? g_key_file_get_string(kf, "audio", "device", NULL)
                            : g_strdup("automatic");
    gchar *audio_parameter = loaded && g_key_file_has_key(kf, "audio", "parameter", NULL) ? g_key_file_get_string(kf, "audio", "parameter", NULL) : g_strdup("strength");
    gdouble audio_sensitivity = loaded && g_key_file_has_key(kf, "audio", "sensitivity", NULL) ? g_key_file_get_double(kf, "audio", "sensitivity", NULL) : 2.0;
    gdouble audio_smoothing = loaded && g_key_file_has_key(kf, "audio", "smoothing", NULL) ? g_key_file_get_double(kf, "audio", "smoothing", NULL) : 0.82;
    gint fps = loaded ? g_key_file_get_integer(kf, "playback", "fps_limit", NULL) : 0;
    gboolean interpolation = loaded ? g_key_file_get_boolean(kf, "advanced", "interpolation", NULL) : FALSE;
    gboolean pause_fullscreen = loaded && g_key_file_has_key(kf, "advanced", "pause_fullscreen", NULL) ? g_key_file_get_boolean(kf, "advanced", "pause_fullscreen", NULL) : TRUE;
    gboolean pause_battery = loaded && g_key_file_has_key(kf, "advanced", "pause_battery", NULL) ? g_key_file_get_boolean(kf, "advanced", "pause_battery", NULL) : FALSE;
    gdouble brightness = loaded ? g_key_file_get_double(kf, "advanced", "brightness", NULL) : 0.0;
    gdouble contrast = loaded ? g_key_file_get_double(kf, "advanced", "contrast", NULL) : 0.0;
    gdouble saturation = loaded ? g_key_file_get_double(kf, "advanced", "saturation", NULL) : 0.0;
    if (video && *video)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(app->file_button), video);
    if (stream_url && *stream_url)
        gtk_entry_set_text(GTK_ENTRY(app->stream_entry), stream_url);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
        g_strcmp0(source, "stream") == 0 ? app->source_stream : app->source_local), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->reconnect_check), reconnect);
    update_source_controls(app);

    gtk_range_set_value(GTK_RANGE(app->speed_scale), speed > 0 ? speed : 1.0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->mute_check), mute);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->loop_check), loop);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hwdec_check), hwdec);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->desktop_icons_check), desktop_icons);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->audio_enabled_check), audio_enabled);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->audio_waveform_check), audio_waveform);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->audio_source_combo),
                             g_strcmp0(audio_source, "overall") == 0 ? 1 : 0);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->audio_device_combo),
                                     audio_device))
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->audio_device_combo), 0);
    gtk_range_set_value(GTK_RANGE(app->audio_sensitivity_scale), CLAMP(audio_sensitivity, 0.1, 10.0));
    gtk_range_set_value(GTK_RANGE(app->audio_smoothing_scale), CLAMP(audio_smoothing, 0.0, 0.98));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->fps_spin), fps);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->interpolation_check), interpolation);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->pause_fullscreen_check), pause_fullscreen);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->pause_battery_check), pause_battery);
    gtk_range_set_value(GTK_RANGE(app->brightness_scale), brightness);
    gtk_range_set_value(GTK_RANGE(app->contrast_scale), contrast);
    gtk_range_set_value(GTK_RANGE(app->saturation_scale), saturation);
    gboolean found_effect=FALSE;
    app->changing_effects=TRUE;
    for(guint i=0;app->effects&&i<app->effects->len;i++){
        EffectDef *e=g_ptr_array_index(app->effects,i);EffectParam *a=effect_activation_param(e);
        gchar *group=g_strdup_printf("effect.%s",e->id);
        for(guint j=0;j<e->params->len;j++){
            EffectParam *p=g_ptr_array_index(e->params,j);gdouble v=p->default_value;
            if(loaded&&g_key_file_has_key(kf,group,p->id,NULL))v=g_key_file_get_double(kf,group,p->id,NULL);
            else if(p==a&&loaded&&g_key_file_has_key(kf,"effects",e->id,NULL))v=g_key_file_get_double(kf,"effects",e->id,NULL);
            v=CLAMP(v,p->min,p->max);
            if(p==a&&v>p->min+0.001){if(found_effect)v=p->min;else found_effect=TRUE;}
            gtk_range_set_value(GTK_RANGE(p->slider),v);
        }g_free(group);
    }
    app->changing_effects=FALSE;
    refresh_audio_effect_controls(app);
    if (audio_parameter && *audio_parameter)
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->audio_parameter_combo), audio_parameter);
    g_free(audio_parameter);
    g_free(audio_source);
    g_free(audio_device);

    if (g_strcmp0(mode, "fit") == 0)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->mode_fit), TRUE);
    else if (g_strcmp0(mode, "stretch") == 0)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->mode_stretch), TRUE);
    else
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->mode_fill), TRUE);

    gchar *apath = autostart_path();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->autostart_check),
                                 g_file_test(apath, G_FILE_TEST_EXISTS));
    g_free(apath);

    g_free(video);
    g_free(stream_url);
    g_free(source);
    g_key_file_unref(kf);
    g_free(path);

    app->loading = FALSE;
    app->settings_dirty = FALSE;
    schedule_preview_restart(app);
    update_status(app);
}

static GtkWidget *row(const gchar *title, const gchar *subtitle, GtkWidget *control) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *t = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(t), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(t), 0.0);
    gtk_box_pack_start(GTK_BOX(text), t, FALSE, FALSE, 0);

    if (subtitle) {
        GtkWidget *s = gtk_label_new(subtitle);
        gtk_label_set_xalign(GTK_LABEL(s), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(s), "dim-label");
        gtk_box_pack_start(GTK_BOX(text), s, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), control, FALSE, FALSE, 0);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    return box;
}


static gboolean on_effect_text_clicked(GtkWidget *widget,
                                       GdkEventButton *event,
                                       gpointer data) {
    (void)widget;

    if (event->button != 1 || event->type != GDK_BUTTON_PRESS)
        return FALSE;

    GtkExpander *expander = GTK_EXPANDER(data);
    gtk_expander_set_expanded(
        expander,
        !gtk_expander_get_expanded(expander));

    return TRUE;
}

static GtkWidget *effect_row(EffectDef *effect,
                             GtkWidget *control,
                             GtkWidget *expander) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget *image = NULL;
    if (effect->icon_path) {
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(
            effect->icon_path, 42, 42, TRUE, &error);
        if (pixbuf) {
            image = gtk_image_new_from_pixbuf(pixbuf);
            g_object_unref(pixbuf);
        }
        g_clear_error(&error);
    }

    if (!image) {
        image = gtk_image_new_from_icon_name("applications-graphics",
                                             GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(image), 42);
    }

    gtk_widget_set_size_request(image, 46, 46);
    gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(outer), image, FALSE, FALSE, 0);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    GtkWidget *title = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", effect->name);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(text), title, FALSE, FALSE, 0);

    if (effect->description && *effect->description) {
        GtkWidget *description = gtk_label_new(effect->description);
        gtk_label_set_xalign(GTK_LABEL(description), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(description), "dim-label");
        gtk_box_pack_start(GTK_BOX(text), description, FALSE, FALSE, 0);
    }

    if (expander) {
        GtkWidget *click_area = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(click_area), FALSE);
        gtk_widget_add_events(click_area, GDK_BUTTON_PRESS_MASK);
        gtk_widget_set_tooltip_text(click_area,
                                    "Click to show or hide effect parameters");
        gtk_container_add(GTK_CONTAINER(click_area), text);
        g_signal_connect(click_area, "button-press-event",
                         G_CALLBACK(on_effect_text_clicked), expander);
        gtk_box_pack_start(GTK_BOX(outer), click_area, TRUE, TRUE, 0);
    } else {
        gtk_box_pack_start(GTK_BOX(outer), text, TRUE, TRUE, 0);
    }

    gtk_box_pack_end(GTK_BOX(outer), control, FALSE, FALSE, 0);
    gtk_widget_set_margin_top(outer, 10);
    gtk_widget_set_margin_bottom(outer, 10);
    return outer;
}

static GtkWidget *centered_check_group(void) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(outer, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(outer, 10);
    gtk_widget_set_margin_bottom(outer, 10);
    gtk_box_pack_start(GTK_BOX(outer), inner, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(outer), "check-group-inner", inner);
    return outer;
}

static void centered_check_group_add(GtkWidget *group, GtkWidget *check) {
    GtkWidget *inner = g_object_get_data(G_OBJECT(group), "check-group-inner");
    gtk_widget_set_halign(check, GTK_ALIGN_START);
    gtk_widget_set_margin_top(check, 5);
    gtk_widget_set_margin_bottom(check, 5);
    gtk_box_pack_start(GTK_BOX(inner), check, FALSE, FALSE, 0);
}

static void speed_fill_changed(GtkRange *range, gpointer user_data) {
    (void) user_data;
    gtk_range_set_fill_level(range, gtk_range_get_value(range));
}


static gchar *presets_dir(void) {
    return g_build_filename(g_get_user_config_dir(),
                            "xfce-animated-wallpaper",
                            "presets", NULL);
}

static gchar *preset_safe_filename(const gchar *name) {
    GString *safe = g_string_new(NULL);

    for (const gchar *p = name; p && *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);

        if (g_unichar_isalnum(c) || c == '-' || c == '_' || c == ' ') {
            gchar buf[7] = {0};
            gint n = g_unichar_to_utf8(c, buf);
            g_string_append_len(safe, buf, n);
        } else {
            g_string_append_c(safe, '_');
        }
    }

    g_strstrip(safe->str);

    if (safe->len == 0)
        g_string_assign(safe, "Preset");

    gchar *filename = g_strdup_printf("%s.ini", safe->str);
    g_string_free(safe, TRUE);
    return filename;
}

static EffectDef *effect_by_id(App *app, const gchar *id) {
    if (!id || !*id)
        return NULL;

    for (guint i = 0; app->effects && i < app->effects->len; i++) {
        EffectDef *e = g_ptr_array_index(app->effects, i);
        if (g_strcmp0(e->id, id) == 0)
            return e;
    }

    return NULL;
}

static EffectParam *effect_param_by_id(EffectDef *effect, const gchar *id) {
    if (!effect || !id)
        return NULL;

    for (guint i = 0; i < effect->params->len; i++) {
        EffectParam *p = g_ptr_array_index(effect->params, i);
        if (g_strcmp0(p->id, id) == 0)
            return p;
    }

    return NULL;
}

static gchar *preset_selected_path(App *app) {
    GtkListBoxRow *row =
        gtk_list_box_get_selected_row(GTK_LIST_BOX(app->preset_list));

    if (!row)
        return NULL;

    const gchar *path =
        g_object_get_data(G_OBJECT(row), "preset-path");

    return path ? g_strdup(path) : NULL;
}

static gchar *preset_summary_from_file(App *app, const gchar *path) {
    GKeyFile *kf = g_key_file_new();
    GError *error = NULL;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &error)) {
        gchar *text = g_strdup_printf("Could not read preset:\n%s",
                                      error ? error->message : "Unknown error");
        g_clear_error(&error);
        g_key_file_unref(kf);
        return text;
    }

    gchar *name = g_key_file_get_string(kf, "Preset", "name", NULL);
    gchar *effect_id = g_key_file_get_string(kf, "Effect", "active", NULL);
    EffectDef *effect = effect_by_id(app, effect_id);

    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "Preset: %s\n\n",
                           name && *name ? name : "Unnamed");

    if (effect_id && *effect_id) {
        g_string_append_printf(out, "Effect: %s\n",
                               effect ? effect->name : effect_id);

        gchar *group = g_strdup_printf("effect.%s", effect_id);

        if (effect) {
            for (guint i = 0; i < effect->params->len; i++) {
                EffectParam *p = g_ptr_array_index(effect->params, i);
                if (!g_key_file_has_key(kf, group, p->id, NULL))
                    continue;

                gdouble value =
                    g_key_file_get_double(kf, group, p->id, NULL);
                g_string_append_printf(out, "%s: %.*f\n",
                                       p->name, p->digits, value);
            }
        }

        g_free(group);
    } else {
        g_string_append(out, "Effect: None\n");
    }

    g_string_append(out, "\nAudio Visualizer\n");

    gboolean enabled =
        g_key_file_has_key(kf, "Audio", "enabled", NULL) &&
        g_key_file_get_boolean(kf, "Audio", "enabled", NULL);
    gboolean waveform =
        g_key_file_has_key(kf, "Audio", "show_waveform", NULL) &&
        g_key_file_get_boolean(kf, "Audio", "show_waveform", NULL);

    gchar *source = g_key_file_has_key(kf, "Audio", "source", NULL)
        ? g_key_file_get_string(kf, "Audio", "source", NULL)
        : g_strdup("bass");
    gchar *parameter = g_key_file_has_key(kf, "Audio", "parameter", NULL)
        ? g_key_file_get_string(kf, "Audio", "parameter", NULL)
        : g_strdup("strength");
    gchar *device = g_key_file_has_key(kf, "Audio", "device", NULL)
        ? g_key_file_get_string(kf, "Audio", "device", NULL)
        : g_strdup("automatic");

    gdouble sensitivity =
        g_key_file_has_key(kf, "Audio", "sensitivity", NULL)
            ? g_key_file_get_double(kf, "Audio", "sensitivity", NULL)
            : 2.0;
    gdouble smoothing =
        g_key_file_has_key(kf, "Audio", "smoothing", NULL)
            ? g_key_file_get_double(kf, "Audio", "smoothing", NULL)
            : 0.82;

    const gchar *parameter_name = parameter;
    if (effect) {
        EffectParam *p = effect_param_by_id(effect, parameter);
        if (p)
            parameter_name = p->name;
    }

    g_string_append_printf(out,
        "Audio reactive: %s\n"
        "Waveform overlay: %s\n"
        "Audio source: %s\n"
        "Audio device: %s\n"
        "Controlled parameter: %s\n"
        "Sensitivity: %.1f\n"
        "Smoothing: %.2f\n",
        enabled ? "On" : "Off",
        waveform ? "On" : "Off",
        g_strcmp0(source, "overall") == 0 ? "Overall level" : "Bass",
        g_strcmp0(device, "automatic") == 0
            ? "Automatic (active output)" : device,
        parameter_name ? parameter_name : "Strength",
        sensitivity,
        smoothing);

    g_free(name);
    g_free(effect_id);
    g_free(source);
    g_free(parameter);
    g_free(device);
    g_key_file_unref(kf);

    return g_string_free(out, FALSE);
}

static void update_preset_values(App *app) {
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->preset_values_view));
    gchar *path = preset_selected_path(app);

    if (!path) {
        gtk_text_buffer_set_text(
            buffer,
            "Select a preset to see its saved values.",
            -1);
        gtk_widget_set_sensitive(app->preset_load_button, FALSE);
        gtk_widget_set_sensitive(app->preset_delete_button, FALSE);
        return;
    }

    gchar *summary = preset_summary_from_file(app, path);
    gtk_text_buffer_set_text(buffer, summary, -1);
    gtk_widget_set_sensitive(app->preset_load_button, TRUE);
    gtk_widget_set_sensitive(app->preset_delete_button, TRUE);

    g_free(summary);
    g_free(path);
}

static void on_preset_selected(GtkListBox *box,
                               GtkListBoxRow *row,
                               gpointer data) {
    (void)box;
    (void)row;
    update_preset_values((App *)data);
}

static gint preset_row_sort(GtkListBoxRow *a,
                            GtkListBoxRow *b,
                            gpointer data) {
    (void)data;

    GtkWidget *ca = gtk_bin_get_child(GTK_BIN(a));
    GtkWidget *cb = gtk_bin_get_child(GTK_BIN(b));

    const gchar *ta = GTK_IS_LABEL(ca)
        ? gtk_label_get_text(GTK_LABEL(ca)) : "";
    const gchar *tb = GTK_IS_LABEL(cb)
        ? gtk_label_get_text(GTK_LABEL(cb)) : "";

    return g_utf8_collate(ta, tb);
}

static void refresh_presets(App *app) {
    GList *children =
        gtk_container_get_children(GTK_CONTAINER(app->preset_list));

    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    gchar *dir_path = presets_dir();
    g_mkdir_with_parents(dir_path, 0700);

    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (dir) {
        const gchar *entry;

        while ((entry = g_dir_read_name(dir))) {
            if (!g_str_has_suffix(entry, ".ini"))
                continue;

            gchar *path = g_build_filename(dir_path, entry, NULL);
            GKeyFile *kf = g_key_file_new();
            gchar *name = NULL;

            if (g_key_file_load_from_file(
                    kf, path, G_KEY_FILE_NONE, NULL)) {
                name = g_key_file_get_string(
                    kf, "Preset", "name", NULL);
            }

            if (!name || !*name) {
                g_free(name);
                name = g_strdup(entry);
                gchar *dot = g_strrstr(name, ".ini");
                if (dot)
                    *dot = '\0';
            }

            GtkWidget *label = gtk_label_new(name);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_widget_set_margin_start(label, 8);
            gtk_widget_set_margin_end(label, 8);
            gtk_widget_set_margin_top(label, 6);
            gtk_widget_set_margin_bottom(label, 6);

            GtkWidget *row = gtk_list_box_row_new();
            gtk_container_add(GTK_CONTAINER(row), label);
            g_object_set_data_full(
                G_OBJECT(row), "preset-path",
                g_strdup(path), g_free);
            gtk_container_add(GTK_CONTAINER(app->preset_list), row);

            g_free(name);
            g_key_file_unref(kf);
            g_free(path);
        }

        g_dir_close(dir);
    }

    gtk_list_box_set_sort_func(
        GTK_LIST_BOX(app->preset_list),
        preset_row_sort, NULL, NULL);

    gtk_widget_show_all(app->preset_list);
    gtk_widget_set_sensitive(app->preset_load_button, FALSE);
    gtk_widget_set_sensitive(app->preset_delete_button, FALSE);

    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->preset_values_view));
    gtk_text_buffer_set_text(
        buffer,
        "Select a preset to see its saved values.",
        -1);

    g_free(dir_path);
}

static gboolean save_preset_file(App *app,
                                 const gchar *name,
                                 const gchar *path,
                                 GError **error) {
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_string(kf, "Preset", "name", name);
    g_key_file_set_integer(kf, "Preset", "version", 1);

    EffectDef *effect = active_effect(app);
    g_key_file_set_string(
        kf, "Effect", "active", effect ? effect->id : "");

    if (effect) {
        gchar *group = g_strdup_printf("effect.%s", effect->id);

        for (guint i = 0; i < effect->params->len; i++) {
            EffectParam *p = g_ptr_array_index(effect->params, i);
            g_key_file_set_double(
                kf, group, p->id,
                gtk_range_get_value(GTK_RANGE(p->slider)));
        }

        g_free(group);
    }

    g_key_file_set_boolean(
        kf, "Audio", "enabled",
        gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->audio_enabled_check)));
    g_key_file_set_boolean(
        kf, "Audio", "show_waveform",
        gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->audio_waveform_check)));

    gchar *source_text =
        gtk_combo_box_text_get_active_text(
            GTK_COMBO_BOX_TEXT(app->audio_source_combo));
    g_key_file_set_string(
        kf, "Audio", "source",
        source_text &&
        g_strcmp0(source_text, "Overall level") == 0
            ? "overall" : "bass");
    g_free(source_text);

    const gchar *device_id =
        gtk_combo_box_get_active_id(
            GTK_COMBO_BOX(app->audio_device_combo));
    g_key_file_set_string(
        kf, "Audio", "device",
        device_id ? device_id : "automatic");

    const gchar *parameter =
        gtk_combo_box_get_active_id(
            GTK_COMBO_BOX(app->audio_parameter_combo));
    g_key_file_set_string(
        kf, "Audio", "parameter",
        parameter ? parameter : "strength");

    g_key_file_set_double(
        kf, "Audio", "sensitivity",
        gtk_range_get_value(
            GTK_RANGE(app->audio_sensitivity_scale)));
    g_key_file_set_double(
        kf, "Audio", "smoothing",
        gtk_range_get_value(
            GTK_RANGE(app->audio_smoothing_scale)));

    gsize length = 0;
    gchar *data = g_key_file_to_data(kf, &length, error);
    gboolean ok = FALSE;

    if (data)
        ok = g_file_set_contents(path, data, length, error);

    g_free(data);
    g_key_file_unref(kf);
    return ok;
}

static void on_save_preset_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Save new preset",
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        NULL);

    GtkWidget *content =
        gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();

    gtk_entry_set_placeholder_text(
        GTK_ENTRY(entry), "Preset name");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(
        GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_top(entry, 12);
    gtk_widget_set_margin_bottom(entry, 12);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *raw_name =
            gtk_entry_get_text(GTK_ENTRY(entry));
        gchar *name = g_strdup(raw_name ? raw_name : "");
        g_strstrip(name);

        if (*name) {
            gchar *dir = presets_dir();
            g_mkdir_with_parents(dir, 0700);
            gchar *filename = preset_safe_filename(name);
            gchar *path = g_build_filename(dir, filename, NULL);

            gboolean save = TRUE;

            if (g_file_test(path, G_FILE_TEST_EXISTS)) {
                GtkWidget *confirm =
                    gtk_message_dialog_new(
                        GTK_WINDOW(dialog),
                        GTK_DIALOG_MODAL,
                        GTK_MESSAGE_QUESTION,
                        GTK_BUTTONS_NONE,
                        "A preset named â€œ%sâ€ already exists.",
                        name);
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(confirm),
                    "Replace the existing preset?");
                gtk_dialog_add_buttons(
                    GTK_DIALOG(confirm),
                    "Cancel", GTK_RESPONSE_CANCEL,
                    "Replace", GTK_RESPONSE_ACCEPT,
                    NULL);

                save =
                    gtk_dialog_run(GTK_DIALOG(confirm)) ==
                    GTK_RESPONSE_ACCEPT;
                gtk_widget_destroy(confirm);
            }

            if (save) {
                GError *error = NULL;

                if (!save_preset_file(app, name, path, &error)) {
                    GtkWidget *error_dialog =
                        gtk_message_dialog_new(
                            GTK_WINDOW(app->window),
                            GTK_DIALOG_MODAL,
                            GTK_MESSAGE_ERROR,
                            GTK_BUTTONS_CLOSE,
                            "Could not save preset");
                    gtk_message_dialog_format_secondary_text(
                        GTK_MESSAGE_DIALOG(error_dialog),
                        "%s",
                        error ? error->message : "Unknown error");
                    gtk_dialog_run(GTK_DIALOG(error_dialog));
                    gtk_widget_destroy(error_dialog);
                    g_clear_error(&error);
                } else {
                    refresh_presets(app);
                }
            }

            g_free(path);
            g_free(filename);
            g_free(dir);
        }

        g_free(name);
    }

    gtk_widget_destroy(dialog);
}

static void on_delete_preset_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;

    gchar *path = preset_selected_path(app);
    if (!path)
        return;

    GtkListBoxRow *row =
        gtk_list_box_get_selected_row(GTK_LIST_BOX(app->preset_list));

    const gchar *display_name = "this preset";
    if (row) {
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
        if (GTK_IS_LABEL(child)) {
            const gchar *text = gtk_label_get_text(GTK_LABEL(child));
            if (text && *text)
                display_name = text;
        }
    }

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE,
        "Delete preset â€œ%sâ€?",
        display_name);

    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "This cannot be undone.");

    gtk_dialog_add_buttons(
        GTK_DIALOG(dialog),
        "Cancel", GTK_RESPONSE_CANCEL,
        "Delete", GTK_RESPONSE_ACCEPT,
        NULL);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_ACCEPT) {
        if (g_unlink(path) != 0) {
            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(app->window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Could not delete preset");

            gtk_message_dialog_format_secondary_text(
                GTK_MESSAGE_DIALOG(error_dialog),
                "%s",
                g_strerror(errno));

            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
        } else {
            refresh_presets(app);
        }
    }

    g_free(path);
}

static void on_load_preset_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;

    gchar *path = preset_selected_path(app);
    if (!path)
        return;

    GKeyFile *kf = g_key_file_new();
    GError *error = NULL;

    if (!g_key_file_load_from_file(
            kf, path, G_KEY_FILE_NONE, &error)) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(app->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "Could not load preset");
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog), "%s",
            error ? error->message : "Unknown error");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_clear_error(&error);
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    gchar *effect_id =
        g_key_file_get_string(kf, "Effect", "active", NULL);
    EffectDef *effect = effect_by_id(app, effect_id);

    if (effect_id && *effect_id && !effect) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(app->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_CLOSE,
            "This preset uses an effect that is not installed");
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "Missing effect: %s", effect_id);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(effect_id);
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    app->loading = TRUE;
    app->changing_effects = TRUE;

    /* Presets select exactly one effect, matching the normal UI rule. */
    for (guint i = 0; app->effects && i < app->effects->len; i++) {
        EffectDef *e = g_ptr_array_index(app->effects, i);
        EffectParam *activation = effect_activation_param(e);

        if (activation)
            gtk_range_set_value(
                GTK_RANGE(activation->slider), activation->min);
    }

    if (effect) {
        gchar *group = g_strdup_printf("effect.%s", effect->id);

        for (guint i = 0; i < effect->params->len; i++) {
            EffectParam *p = g_ptr_array_index(effect->params, i);

            if (!g_key_file_has_key(kf, group, p->id, NULL))
                continue;

            gdouble value =
                g_key_file_get_double(kf, group, p->id, NULL);
            gtk_range_set_value(
                GTK_RANGE(p->slider),
                CLAMP(value, p->min, p->max));
        }

        g_free(group);
    }

    app->changing_effects = FALSE;

    gboolean enabled =
        g_key_file_has_key(kf, "Audio", "enabled", NULL)
            ? g_key_file_get_boolean(kf, "Audio", "enabled", NULL)
            : FALSE;
    gboolean waveform =
        g_key_file_has_key(kf, "Audio", "show_waveform", NULL)
            ? g_key_file_get_boolean(kf, "Audio", "show_waveform", NULL)
            : FALSE;
    gchar *source =
        g_key_file_has_key(kf, "Audio", "source", NULL)
            ? g_key_file_get_string(kf, "Audio", "source", NULL)
            : g_strdup("bass");
    gchar *device =
        g_key_file_has_key(kf, "Audio", "device", NULL)
            ? g_key_file_get_string(kf, "Audio", "device", NULL)
            : g_strdup("automatic");
    gchar *parameter =
        g_key_file_has_key(kf, "Audio", "parameter", NULL)
            ? g_key_file_get_string(kf, "Audio", "parameter", NULL)
            : g_strdup("strength");
    gdouble sensitivity =
        g_key_file_has_key(kf, "Audio", "sensitivity", NULL)
            ? g_key_file_get_double(kf, "Audio", "sensitivity", NULL)
            : 2.0;
    gdouble smoothing =
        g_key_file_has_key(kf, "Audio", "smoothing", NULL)
            ? g_key_file_get_double(kf, "Audio", "smoothing", NULL)
            : 0.82;

    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(app->audio_enabled_check), enabled);
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(app->audio_waveform_check), waveform);
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(app->audio_source_combo),
        g_strcmp0(source, "overall") == 0 ? 1 : 0);
    if (!gtk_combo_box_set_active_id(
            GTK_COMBO_BOX(app->audio_device_combo), device))
        gtk_combo_box_set_active(
            GTK_COMBO_BOX(app->audio_device_combo), 0);
    gtk_range_set_value(
        GTK_RANGE(app->audio_sensitivity_scale),
        CLAMP(sensitivity, 0.1, 10.0));
    gtk_range_set_value(
        GTK_RANGE(app->audio_smoothing_scale),
        CLAMP(smoothing, 0.0, 0.98));

    refresh_audio_effect_controls(app);

    if (!gtk_combo_box_set_active_id(
            GTK_COMBO_BOX(app->audio_parameter_combo),
            parameter)) {
        gtk_combo_box_set_active(
            GTK_COMBO_BOX(app->audio_parameter_combo), 0);
    }

    app->loading = FALSE;
    app->settings_dirty = TRUE;

    save_config(app);
    update_status(app);

    if (app->status_indicator)
        gtk_widget_queue_draw(app->status_indicator);

    schedule_preview_restart(app);
    update_preset_values(app);

    g_free(source);
    g_free(device);
    g_free(parameter);
    g_free(effect_id);
    g_key_file_unref(kf);
    g_free(path);
}


static gchar *friendly_audio_device_name(const gchar *source) {
    if (!source || !*source)
        return g_strdup("Unknown audio output");

    gchar *lower = g_ascii_strdown(source, -1);

    if (strstr(lower, "snd_aloop") || strstr(lower, "loopback")) {
        g_free(lower);
        return g_strdup("ALSA Loopback");
    }

    if (strstr(lower, "hdmi")) {
        g_free(lower);
        return g_strdup("HDMI / DisplayPort");
    }

    if (strstr(lower, "analog-stereo")) {
        g_free(lower);
        return g_strdup("Built-in Analog Stereo");
    }

    if (strstr(lower, "usb")) {
        g_free(lower);
        return g_strdup("USB Audio");
    }

    g_free(lower);

    /* Fallback: compact the monitor name rather than showing the full ID. */
    gchar *name = g_strdup(source);
    gchar *monitor = g_strrstr(name, ".monitor");
    if (monitor)
        *monitor = '\0';

    if (strlen(name) > 30) {
        name[27] = '\0';
        gchar *short_name = g_strconcat(name, "â€¦", NULL);
        g_free(name);
        return short_name;
    }

    return name;
}

static void populate_audio_device_combo(GtkComboBoxText *combo) {
    gtk_combo_box_text_remove_all(combo);
    gtk_combo_box_text_append(combo, "automatic", "Automatic (active output)");

    gchar *out = NULL;
    gint status = 0;
    GError *error = NULL;
    gchar *argv[] = {
        (gchar *)"pactl", (gchar *)"list",
        (gchar *)"short", (gchar *)"sources", NULL
    };

    if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                     NULL, NULL, &out, NULL, &status, &error) &&
        g_spawn_check_wait_status(status, NULL)) {
        gchar **lines = g_strsplit(out ? out : "", "\n", -1);

        for (guint i = 0; lines && lines[i]; i++) {
            if (!*lines[i])
                continue;

            gchar **fields = g_strsplit(lines[i], "\t", 0);
            if (fields && fields[1] &&
                g_str_has_suffix(fields[1], ".monitor")) {
                gchar *friendly =
                    friendly_audio_device_name(fields[1]);
                gtk_combo_box_text_append(
                    combo, fields[1], friendly);
                g_free(friendly);
            }
            g_strfreev(fields);
        }

        g_strfreev(lines);
    }

    g_clear_error(&error);
    g_free(out);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}


static GPid runtime_pid_from_file(const gchar *filename) {
    const gchar *runtime = g_get_user_runtime_dir();
    gchar *path = g_build_filename(
        runtime ? runtime : "/tmp",
        filename,
        NULL);

    gchar *text = NULL;
    GPid pid = 0;

    if (g_file_get_contents(path, &text, NULL, NULL) && text)
        pid = (GPid)g_ascii_strtoll(text, NULL, 10);

    g_free(text);
    g_free(path);
    return pid;
}

static void pause_background_for_fullscreen(App *app) {
    app->fullscreen_paused_preview = FALSE;
    app->fullscreen_paused_preview_audio = FALSE;
    app->fullscreen_paused_wallpaper = FALSE;
    app->fullscreen_paused_wallpaper_audio = FALSE;

    /*
     * Preview is launched in its own process group. Stop the group so both
     * the shell adapter and mpv genuinely stop consuming rendering time.
     */
    if (app->preview_pid > 1 && kill(-app->preview_pid, 0) == 0) {
        if (kill(-app->preview_pid, SIGSTOP) == 0)
            app->fullscreen_paused_preview = TRUE;
    }

    if (app->preview_audio_pid > 1 &&
        kill(app->preview_audio_pid, 0) == 0) {
        if (kill(app->preview_audio_pid, SIGSTOP) == 0)
            app->fullscreen_paused_preview_audio = TRUE;
    }

    /*
     * The wallpaper PID file contains the xwinwrap process-group leader.
     * Pausing the whole group stops xwinwrap, its shell, and wallpaper mpv.
     */
    GPid wallpaper_pgid =
        runtime_pid_from_file("xfce-animated-wallpaper.pid");

    if (wallpaper_pgid > 1 &&
        kill(-wallpaper_pgid, 0) == 0) {
        if (kill(-wallpaper_pgid, SIGSTOP) == 0)
            app->fullscreen_paused_wallpaper = TRUE;
    }

    GPid visualizer_pid =
        runtime_pid_from_file(
            "xfce-animated-wallpaper-visualizer.pid");

    if (visualizer_pid > 1 &&
        kill(visualizer_pid, 0) == 0) {
        if (kill(visualizer_pid, SIGSTOP) == 0)
            app->fullscreen_paused_wallpaper_audio = TRUE;
    }
}

static void resume_background_after_fullscreen(App *app) {
    if (app->fullscreen_paused_preview &&
        app->preview_pid > 1)
        kill(-app->preview_pid, SIGCONT);

    if (app->fullscreen_paused_preview_audio &&
        app->preview_audio_pid > 1)
        kill(app->preview_audio_pid, SIGCONT);

    if (app->fullscreen_paused_wallpaper) {
        GPid wallpaper_pgid =
            runtime_pid_from_file(
                "xfce-animated-wallpaper.pid");

        if (wallpaper_pgid > 1)
            kill(-wallpaper_pgid, SIGCONT);
    }

    if (app->fullscreen_paused_wallpaper_audio) {
        GPid visualizer_pid =
            runtime_pid_from_file(
                "xfce-animated-wallpaper-visualizer.pid");

        if (visualizer_pid > 1)
            kill(visualizer_pid, SIGCONT);
    }

    app->fullscreen_paused_preview = FALSE;
    app->fullscreen_paused_preview_audio = FALSE;
    app->fullscreen_paused_wallpaper = FALSE;
    app->fullscreen_paused_wallpaper_audio = FALSE;
}

static gchar *fullscreen_audio_ipc_path(void) {
    const gchar *runtime = g_get_user_runtime_dir();

    return g_build_filename(
        runtime ? runtime : "/tmp",
        "xfce-animated-wallpaper-fullscreen-mpv.sock",
        NULL);
}

static void stop_fullscreen_audio(App *app) {
    if (app->fullscreen_audio_pid > 1) {
        kill(app->fullscreen_audio_pid, SIGTERM);
        g_usleep(50000);
        g_spawn_close_pid(app->fullscreen_audio_pid);
        app->fullscreen_audio_pid = 0;
    }

    if (app->fullscreen_ipc_path) {
        g_unlink(app->fullscreen_ipc_path);
        g_free(app->fullscreen_ipc_path);
        app->fullscreen_ipc_path = NULL;
    }
}

static gboolean start_fullscreen_audio(App *app) {
    stop_fullscreen_audio(app);

    app->fullscreen_ipc_path =
        fullscreen_audio_ipc_path();

    g_unlink(app->fullscreen_ipc_path);

    gchar *argv[] = {
        (gchar *)"xfce-animated-wallpaper-visualizer",
        (gchar *)"--control",
        (gchar *)"--no-waveform",
        (gchar *)"--ipc-path",
        app->fullscreen_ipc_path,
        NULL
    };

    GError *error = NULL;
    GPid pid = 0;

    gboolean ok = g_spawn_async(
        NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH |
        G_SPAWN_DO_NOT_REAP_CHILD |
        G_SPAWN_STDOUT_TO_DEV_NULL |
        G_SPAWN_STDERR_TO_DEV_NULL,
        NULL,
        NULL,
        &pid,
        &error);

    if (!ok) {
        g_clear_error(&error);
        stop_fullscreen_audio(app);
        return FALSE;
    }

    app->fullscreen_audio_pid = pid;
    return TRUE;
}

static void fullscreen_child_exited(GPid pid, gint status, gpointer data) {
    (void)status;
    App *app = data;

    app->fullscreen_child_watch_source = 0;
    if (app->fullscreen_pid == pid)
        app->fullscreen_pid = 0;

    g_spawn_close_pid(pid);

    if (app->fullscreen_window) {
        GtkWidget *window = app->fullscreen_window;
        app->fullscreen_window = NULL;
        app->fullscreen_socket = NULL;
        gtk_widget_destroy(window);
    }
}

static void stop_fullscreen_showcase(App *app) {
    stop_fullscreen_audio(app);
    GPid pid = app->fullscreen_pid;

    if (app->fullscreen_start_source) {
        g_source_remove(app->fullscreen_start_source);
        app->fullscreen_start_source = 0;
    }

    if (app->fullscreen_child_watch_source) {
        g_source_remove(app->fullscreen_child_watch_source);
        app->fullscreen_child_watch_source = 0;
    }

    if (pid > 1) {
        kill(-pid, SIGTERM);

        gint status = 0;
        gboolean reaped = FALSE;

        for (int i = 0; i < 12; i++) {
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid || result < 0) {
                reaped = TRUE;
                break;
            }
            g_usleep(25000);
        }

        if (!reaped) {
            kill(-pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                ;
        }

        g_spawn_close_pid(pid);
        app->fullscreen_pid = 0;
    }

    if (app->fullscreen_window) {
        GtkWidget *window = app->fullscreen_window;
        app->fullscreen_window = NULL;
        app->fullscreen_socket = NULL;
        gtk_widget_destroy(window);
    }
    resume_background_after_fullscreen(app);
}

static gboolean on_fullscreen_exit_key(GtkWidget *widget,
                                       GdkEventKey *event,
                                       gpointer data) {
    (void)widget;
    (void)event;
    stop_fullscreen_showcase((App *)data);
    return TRUE;
}

static gboolean on_fullscreen_exit_click(GtkWidget *widget,
                                         GdkEventButton *event,
                                         gpointer data) {
    (void)widget;
    (void)event;
    stop_fullscreen_showcase((App *)data);
    return TRUE;
}

static gboolean start_fullscreen_mpv_idle(gpointer data) {
    App *app = data;
    app->fullscreen_start_source = 0;

    if (!app->fullscreen_window ||
        !app->fullscreen_socket ||
        !gtk_widget_get_realized(app->fullscreen_socket))
        return G_SOURCE_REMOVE;

    gchar *video = selected_source(app);
    if (!source_is_valid(app, video)) {
        g_free(video);
        stop_fullscreen_showcase(app);
        return G_SOURCE_REMOVE;
    }

    Window xid = gtk_socket_get_id(GTK_SOCKET(app->fullscreen_socket));

    GtkAllocation fs_alloc;
    gtk_widget_get_allocation(app->fullscreen_socket, &fs_alloc);
    gchar *fs_cache =
        g_build_filename(g_get_user_cache_dir(),
                         "xfce-animated-wallpaper", NULL);
    g_mkdir_with_parents(fs_cache, 0700);
    gchar *fs_log =
        g_build_filename(fs_cache, "fullscreen-debug.log", NULL);
    gchar *fs_line = g_strdup_printf(
        "fullscreen socket xid=0x%lx mapped=%d size=%dx%d\n",
        (unsigned long)xid,
        gtk_widget_get_mapped(app->fullscreen_socket),
        fs_alloc.width, fs_alloc.height);
    g_file_set_contents(fs_log, fs_line, -1, NULL);
    g_free(fs_line);
    g_free(fs_log);
    g_free(fs_cache);

    if (xid == 0) {
        g_free(video);
        stop_fullscreen_showcase(app);
        return G_SOURCE_REMOVE;
    }

    gchar *media = NULL;
    gchar *static_cache = NULL;

    if (!source_is_stream(app) && path_is_static_image(video)) {
        GError *cache_error = NULL;
        static_cache = static_image_cache_video(video, &cache_error);

        if (!static_cache) {
            g_clear_error(&cache_error);
            g_free(video);
            stop_fullscreen_showcase(app);
            return G_SOURCE_REMOVE;
        }

        media = g_strdup(static_cache);
    } else {
        media = g_strdup(video);
    }

    gchar speed_num[G_ASCII_DTOSTR_BUF_SIZE];
    gchar brightness_num[G_ASCII_DTOSTR_BUF_SIZE];
    gchar contrast_num[G_ASCII_DTOSTR_BUF_SIZE];
    gchar saturation_num[G_ASCII_DTOSTR_BUF_SIZE];

    g_ascii_formatd(speed_num, sizeof speed_num, "%.3f",
                    gtk_range_get_value(GTK_RANGE(app->speed_scale)));
    g_ascii_formatd(brightness_num, sizeof brightness_num, "%.0f",
                    gtk_range_get_value(GTK_RANGE(app->brightness_scale)));
    g_ascii_formatd(contrast_num, sizeof contrast_num, "%.0f",
                    gtk_range_get_value(GTK_RANGE(app->contrast_scale)));
    g_ascii_formatd(saturation_num, sizeof saturation_num, "%.0f",
                    gtk_range_get_value(GTK_RANGE(app->saturation_scale)));

    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup("mpv"));
    g_ptr_array_add(argv, g_strdup_printf("--wid=%lu", (unsigned long)xid));
    g_ptr_array_add(argv, g_strdup("--really-quiet"));
    g_ptr_array_add(argv, g_strdup("--no-osc"));
    g_ptr_array_add(argv, g_strdup("--no-input-default-bindings"));
    g_ptr_array_add(argv, g_strdup("--no-border"));
    g_ptr_array_add(argv, g_strdup("--force-window=yes"));
    g_ptr_array_add(argv, g_strdup("--input-cursor-passthrough=yes"));
    g_ptr_array_add(argv, g_strdup("--framedrop=vo"));
    g_ptr_array_add(argv, g_strdup("--vo=gpu-next"));
    g_ptr_array_add(argv, g_strdup("--no-audio"));
    g_ptr_array_add(argv, g_strdup("--loop-file=inf"));
    g_ptr_array_add(argv, g_strdup_printf("--speed=%s", speed_num));
    g_ptr_array_add(argv, g_strdup_printf("--brightness=%s", brightness_num));
    g_ptr_array_add(argv, g_strdup_printf("--contrast=%s", contrast_num));
    g_ptr_array_add(argv, g_strdup_printf("--saturation=%s", saturation_num));

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->hwdec_check)))
        g_ptr_array_add(argv, g_strdup("--hwdec=auto-safe"));
    else
        g_ptr_array_add(argv, g_strdup("--hwdec=no"));

    if (source_is_stream(app)) {
        g_ptr_array_add(argv, g_strdup("--ytdl=yes"));
        g_ptr_array_add(argv,
            g_strdup("--script-opts=ytdl_hook-try_ytdl_first=yes"));
    }

    gboolean fullscreen_audio_enabled =
        app->audio_enabled_check &&
        gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(app->audio_enabled_check));

    if (fullscreen_audio_enabled) {
        stop_fullscreen_audio(app);
        app->fullscreen_ipc_path =
            fullscreen_audio_ipc_path();
        g_unlink(app->fullscreen_ipc_path);

        g_ptr_array_add(
            argv,
            g_strdup_printf(
                "--input-ipc-server=%s",
                app->fullscreen_ipc_path));
    }

    EffectDef *effect = active_effect(app);
    if (effect)
        add_effect_shader(app, argv, effect);

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->mode_stretch))) {
        g_ptr_array_add(argv, g_strdup("--keepaspect=no"));
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->mode_fit))) {
        g_ptr_array_add(argv, g_strdup("--keepaspect=yes"));
        g_ptr_array_add(argv, g_strdup("--panscan=0.0"));
    } else {
        g_ptr_array_add(argv, g_strdup("--keepaspect=yes"));
        g_ptr_array_add(argv, g_strdup("--panscan=1.0"));
    }

    g_ptr_array_add(argv, media);
    g_ptr_array_add(argv, NULL);

    GError *error = NULL;
    GPid pid = 0;
    gboolean ok = g_spawn_async(
        NULL,
        (gchar **)argv->pdata,
        NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        preview_child_setup,
        NULL,
        &pid,
        &error);

    if (ok) {
        app->fullscreen_pid = pid;
        app->fullscreen_child_watch_source =
            g_child_watch_add(pid, fullscreen_child_exited, app);

        if (fullscreen_audio_enabled) {
            /*
             * mpv may need a moment to create the Unix socket, but the
             * visualizer retries its IPC connection every control tick.
             */
            start_fullscreen_audio(app);
        }
    } else {
        g_clear_error(&error);
        stop_fullscreen_showcase(app);
    }

    g_ptr_array_free(argv, TRUE);
    g_free(static_cache);
    g_free(video);
    return G_SOURCE_REMOVE;
}

static gboolean on_fullscreen_socket_click(GtkWidget *widget,
                                           GdkEventButton *event,
                                           gpointer data) {
    (void)widget;
    (void)event;
    stop_fullscreen_showcase((App *)data);
    return TRUE;
}

static void start_fullscreen_showcase(App *app) {
    if (app->fullscreen_window) {
        stop_fullscreen_showcase(app);
        return;
    }

    gchar *video = selected_source(app);
    gboolean valid = source_is_valid(app, video);
    g_free(video);

    if (!valid)
        return;

    pause_background_for_fullscreen(app);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->fullscreen_window = window;

    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), TRUE);
    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);

    /*
     * Keep the hierarchy deliberately simple. GtkSocket is the sole child
     * and fills the entire window; no GtkOverlay/EventBox can paint above it.
     */
    app->fullscreen_socket = gtk_socket_new();
    gtk_widget_set_hexpand(app->fullscreen_socket, TRUE);
    gtk_widget_set_vexpand(app->fullscreen_socket, TRUE);
    gtk_widget_add_events(app->fullscreen_socket, GDK_BUTTON_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(window), app->fullscreen_socket);

    g_signal_connect(
        window, "key-press-event",
        G_CALLBACK(on_fullscreen_exit_key), app);
    g_signal_connect(
        window, "button-press-event",
        G_CALLBACK(on_fullscreen_exit_click), app);
    g_signal_connect(
        app->fullscreen_socket, "button-press-event",
        G_CALLBACK(on_fullscreen_socket_click), app);

    gtk_widget_show_all(window);
    gtk_window_fullscreen(GTK_WINDOW(window));
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(window);

    /*
     * Fullscreening is asynchronous under X11. Waiting a little gives the
     * toplevel and GtkSocket a real, mapped size before mpv receives the XID.
     */
    app->fullscreen_start_source =
        g_timeout_add(180, start_fullscreen_mpv_idle, app);
}

static gboolean on_window_key_press(GtkWidget *widget,
                                    GdkEventKey *event,
                                    gpointer data) {
    (void)widget;
    App *app = data;

    if ((event->state & GDK_SHIFT_MASK) &&
        (event->keyval == GDK_KEY_f ||
         event->keyval == GDK_KEY_F)) {
        start_fullscreen_showcase(app);
        return TRUE;
    }

    switch (event->keyval) {
        case GDK_KEY_1:
        case GDK_KEY_KP_1:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
            return TRUE;
        case GDK_KEY_2:
        case GDK_KEY_KP_2:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 1);
            return TRUE;
        case GDK_KEY_3:
        case GDK_KEY_KP_3:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 2);
            return TRUE;
        case GDK_KEY_4:
        case GDK_KEY_KP_4:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 3);
            return TRUE;
        case GDK_KEY_5:
        case GDK_KEY_KP_5:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 4);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            gtk_button_clicked(GTK_BUTTON(app->set_wallpaper_button));
            return TRUE;
        default:
            return FALSE;
    }
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    App app = {0};
    app.preview_enabled = TRUE;

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Animated Wallpaper");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 920, 650);
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 18);
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_window_destroy), &app);
    g_signal_connect(app.window, "focus-in-event", G_CALLBACK(on_window_focus_in), &app);
    g_signal_connect(app.window, "focus-out-event", G_CALLBACK(on_window_focus_out), &app);
    g_signal_connect(app.window, "configure-event", G_CALLBACK(on_window_configure), &app);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_window_key_press), &app);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_add(GTK_CONTAINER(app.window), root);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_box_pack_start(GTK_BOX(root), content, TRUE, TRUE, 0);

    GtkWidget *notebook = gtk_notebook_new();
    app.notebook = notebook;

    GtkWidget *general_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(general_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *general = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(general), 10);
    gtk_container_add(GTK_CONTAINER(general_scroll), general);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), general_scroll, gtk_label_new("Animated Wallpaper"));

    app.preview_stack = gtk_stack_new();
    gtk_widget_set_size_request(app.preview_stack, 240, 135);
    gtk_widget_set_hexpand(app.preview_stack, FALSE);
    gtk_widget_set_vexpand(app.preview_stack, FALSE);
    gtk_widget_set_halign(app.preview_stack, GTK_ALIGN_CENTER);
    app.preview_area = gtk_socket_new();
    gtk_widget_set_size_request(app.preview_area, 240, 135);
    gtk_widget_set_hexpand(app.preview_area, FALSE);
    gtk_widget_set_vexpand(app.preview_area, FALSE);
    gtk_stack_add_named(GTK_STACK(app.preview_stack), app.preview_area, "video");
    app.preview_label = gtk_label_new("Choose a wallpaper to preview it");
    gtk_widget_set_size_request(app.preview_label, 220, -1);
    gtk_widget_set_hexpand(app.preview_label, FALSE);
    gtk_widget_set_vexpand(app.preview_label, TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(app.preview_label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(app.preview_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(app.preview_label), 28);
    gtk_label_set_justify(GTK_LABEL(app.preview_label), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(app.preview_label), 0.5f);
    gtk_label_set_yalign(GTK_LABEL(app.preview_label), 0.5f);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.preview_label), "dim-label");
    gtk_stack_add_named(GTK_STACK(app.preview_stack), app.preview_label, "message");

    GtkWidget *preview_frame = gtk_frame_new("Preview");
    gtk_frame_set_label_align(GTK_FRAME(preview_frame), 0.5f, 0.5f);

    app.preview_eventbox = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(app.preview_eventbox), FALSE);
    gtk_widget_set_tooltip_text(app.preview_eventbox,
                                "Click to choose a different animated wallpaper");
    gtk_container_add(GTK_CONTAINER(app.preview_eventbox), app.preview_stack);
    gtk_container_add(GTK_CONTAINER(preview_frame), app.preview_eventbox);
    gtk_widget_set_halign(preview_frame, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(preview_frame, GTK_ALIGN_START);
    gtk_widget_set_hexpand(preview_frame, FALSE);
    gtk_widget_set_vexpand(preview_frame, FALSE);
    gtk_widget_set_size_request(preview_frame, 250, -1);
    gtk_widget_set_size_request(app.preview_stack, 240, 135);
    gtk_widget_set_margin_top(preview_frame, 10);
    gtk_widget_set_margin_bottom(preview_frame, 10);
    /* Keep the live preview outside the notebook so it remains visible on every tab. */
    GtkWidget *preview_column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(preview_column, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(preview_column, GTK_ALIGN_CENTER);

    gtk_widget_set_valign(preview_frame, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(preview_column), preview_frame, FALSE, FALSE, 0);

    app.preview_audio_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.preview_audio_label), 0.5);
    gtk_label_set_justify(GTK_LABEL(app.preview_audio_label), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(app.preview_audio_label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(app.preview_audio_label), PANGO_WRAP_WORD_CHAR);
    gtk_widget_set_size_request(app.preview_audio_label, 250, -1);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(app.preview_audio_label), "dim-label");
    gtk_widget_set_no_show_all(app.preview_audio_label, TRUE);
    gtk_widget_hide(app.preview_audio_label);
    gtk_box_pack_start(GTK_BOX(preview_column),
                       app.preview_audio_label, FALSE, FALSE, 0);

    app.preview_toggle_button = gtk_button_new_with_label("Turn off preview");
    gtk_widget_set_size_request(app.preview_toggle_button, 250, -1);
    gtk_widget_set_tooltip_text(app.preview_toggle_button,
                                "Stop or restart the live wallpaper preview.");
    gtk_box_pack_start(GTK_BOX(preview_column),
                       app.preview_toggle_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), preview_column, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), notebook, TRUE, TRUE, 0);

    GtkWidget *source_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(source_box, 270, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(source_box), "linked");
    app.source_local = gtk_radio_button_new_with_label(NULL, "Local file");
    GSList *source_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app.source_local));
    app.source_stream = gtk_radio_button_new_with_label(source_group, "Web URL");
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(app.source_local), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(app.source_stream), FALSE);
    gtk_widget_set_size_request(app.source_local, 120, 30);
    gtk_widget_set_size_request(app.source_stream, 120, 30);
    gtk_box_pack_start(GTK_BOX(source_box), app.source_local, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(source_box), app.source_stream, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.source_local), TRUE);
    gtk_box_pack_start(GTK_BOX(general),
                       row("Wallpaper Source", "Choose a local video, animated image, static image, or a web video source.",
                           source_box),
                       FALSE, FALSE, 0);

    app.file_button = gtk_file_chooser_button_new("Choose wallpaper media", GTK_FILE_CHOOSER_ACTION_OPEN);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Wallpaper media (video and images)");
    gtk_file_filter_add_mime_type(filter, "video/*");
    gtk_file_filter_add_mime_type(filter, "image/*");
    gtk_file_filter_add_pattern(filter, "*.gif");
    gtk_file_filter_add_pattern(filter, "*.GIF");
    gtk_file_filter_add_pattern(filter, "*.apng");
    gtk_file_filter_add_pattern(filter, "*.APNG");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_filter_add_pattern(filter, "*.PNG");
    gtk_file_filter_add_pattern(filter, "*.jpg");
    gtk_file_filter_add_pattern(filter, "*.JPG");
    gtk_file_filter_add_pattern(filter, "*.jpeg");
    gtk_file_filter_add_pattern(filter, "*.JPEG");
    gtk_file_filter_add_pattern(filter, "*.webp");
    gtk_file_filter_add_pattern(filter, "*.WEBP");
    gtk_file_filter_add_pattern(filter, "*.bmp");
    gtk_file_filter_add_pattern(filter, "*.BMP");
    gtk_file_filter_add_pattern(filter, "*.tif");
    gtk_file_filter_add_pattern(filter, "*.TIF");
    gtk_file_filter_add_pattern(filter, "*.tiff");
    gtk_file_filter_add_pattern(filter, "*.TIFF");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(app.file_button), filter);
    GtkWidget *picker_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_size_request(picker_box, 270, -1);
    gtk_widget_set_size_request(app.file_button, -1, 30);
    gtk_box_pack_start(GTK_BOX(picker_box), app.file_button, TRUE, TRUE, 0);
    GtkWidget *gallery_button = gtk_button_new_with_label("Gallery...");
    gtk_widget_set_tooltip_text(gallery_button, "Browse a folder as a grid of wallpaper thumbnails");
    gtk_widget_set_size_request(gallery_button, -1, 30);
    gtk_box_pack_start(GTK_BOX(picker_box), gallery_button, FALSE, FALSE, 0);
    app.wallpaper_row = row("Wallpaper file",
                            "Choose a video, GIF, APNG, or another animated format supported by mpv.",
                            picker_box);
    gtk_box_pack_start(GTK_BOX(general), app.wallpaper_row, FALSE, FALSE, 0);

    app.stream_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.stream_entry),
                                   "https://example.com/live/stream.m3u8");
    gtk_widget_set_size_request(app.stream_entry, 360, 30);
    app.stream_row = row(
        "Wallpaper URL",
        "Direct streams such as HLS (.m3u8), DASH, RTSP, or media URLs are recommended. "
        "Web video URLs such as YouTube are supported through yt-dlp but are experimental and may freeze or reconnect.",
        app.stream_entry);
    gtk_box_pack_start(GTK_BOX(general), app.stream_row, FALSE, FALSE, 0);


    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(mode_box), TRUE);
    gtk_widget_set_size_request(mode_box, 270, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(mode_box), "linked");
    app.mode_fill = gtk_radio_button_new_with_label(NULL, "Fill");
    GSList *mode_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app.mode_fill));
    app.mode_fit = gtk_radio_button_new_with_label(mode_group, "Fit");
    mode_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app.mode_fit));
    app.mode_stretch = gtk_radio_button_new_with_label(mode_group, "Stretch");
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(app.mode_fill), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(app.mode_fit), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(app.mode_stretch), FALSE);
    gtk_widget_set_size_request(app.mode_fill, -1, 30);
    gtk_widget_set_size_request(app.mode_fit, -1, 30);
    gtk_widget_set_size_request(app.mode_stretch, -1, 30);
    gtk_box_pack_start(GTK_BOX(mode_box), app.mode_fill, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(mode_box), app.mode_fit, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(mode_box), app.mode_stretch, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mode_fill), TRUE);
    gtk_box_pack_start(GTK_BOX(general), row("Scaling", "Fill crops edges; Fit preserves the whole image; Stretch ignores aspect ratio.", mode_box), FALSE, FALSE, 0);

    app.speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.2, 2.0, 0.1);
    gtk_widget_set_size_request(app.speed_scale, 210, -1);
    gtk_scale_set_digits(GTK_SCALE(app.speed_scale), 1);
    gtk_range_set_round_digits(GTK_RANGE(app.speed_scale), 1);
    gtk_range_set_show_fill_level(GTK_RANGE(app.speed_scale), TRUE);
    gtk_range_set_restrict_to_fill_level(GTK_RANGE(app.speed_scale), FALSE);
    gtk_range_set_fill_level(GTK_RANGE(app.speed_scale), 1.0);
    g_signal_connect(app.speed_scale, "value-changed", G_CALLBACK(speed_fill_changed), NULL);
    for (gdouble mark = 0.2; mark <= 2.0001; mark += 0.1)
        gtk_scale_add_mark(GTK_SCALE(app.speed_scale), mark, GTK_POS_BOTTOM, NULL);
    gtk_scale_set_value_pos(GTK_SCALE(app.speed_scale), GTK_POS_RIGHT);

    GtkWidget *general_checks = centered_check_group();
    app.mute_check = gtk_check_button_new_with_label("Mute audio");
    centered_check_group_add(general_checks, app.mute_check);
    app.loop_check = gtk_check_button_new_with_label("Loop wallpaper video");
    centered_check_group_add(general_checks, app.loop_check);
    app.reconnect_check = gtk_check_button_new_with_label("Reconnect web video if playback stops");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.reconnect_check), TRUE);
    centered_check_group_add(general_checks, app.reconnect_check);
    app.desktop_icons_check = gtk_check_button_new_with_label("Show desktop icons");
    gtk_widget_set_tooltip_text(app.desktop_icons_check,
                                "Shows items from your XDG Desktop folder above the animated wallpaper.");
    centered_check_group_add(general_checks, app.desktop_icons_check);

    app.autostart_check = gtk_check_button_new_with_label("Start when you log in");
    centered_check_group_add(general_checks, app.autostart_check);
    gtk_box_pack_start(GTK_BOX(general), general_checks, FALSE, FALSE, 0);

    GtkWidget *advanced_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(advanced_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *advanced = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(advanced), 10);
    gtk_container_add(GTK_CONTAINER(advanced_scroll), advanced);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), advanced_scroll, gtk_label_new("Advanced settings"));

    GtkWidget *advanced_checks = centered_check_group();
    app.interpolation_check = gtk_check_button_new_with_label("Smooth motion with frame interpolation");
    gtk_widget_set_tooltip_text(app.interpolation_check, "Uses mpv display-resample synchronization and temporal interpolation. This can increase GPU usage.");
    centered_check_group_add(advanced_checks, app.interpolation_check);

    app.hwdec_check = gtk_check_button_new_with_label("Use hardware video decoding when available");
    gtk_widget_set_tooltip_text(app.hwdec_check, "Lets mpv use hardware video decoding when possible. Built-in effects run in the GPU renderer.");
    centered_check_group_add(advanced_checks, app.hwdec_check);

    app.pause_fullscreen_check = gtk_check_button_new_with_label("Pause when another application is fullscreen");
    gtk_widget_set_tooltip_text(app.pause_fullscreen_check, "Pauses wallpaper rendering while the active X11 window is fullscreen.");
    centered_check_group_add(advanced_checks, app.pause_fullscreen_check);

    app.pause_battery_check = gtk_check_button_new_with_label("Pause while running on battery");
    gtk_widget_set_tooltip_text(app.pause_battery_check, "Pauses wallpaper rendering when a system battery reports Discharging.");
    centered_check_group_add(advanced_checks, app.pause_battery_check);
    gtk_box_pack_start(GTK_BOX(advanced), advanced_checks, FALSE, FALSE, 0);

    app.fps_spin = gtk_spin_button_new_with_range(0, 240, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(app.fps_spin), TRUE);
    gtk_box_pack_start(GTK_BOX(advanced), row("Playback speed", "Useful for slowing down subtle ambient loops.", app.speed_scale), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(advanced), row("FPS limit", "0 uses the wallpaper's normal frame rate.", app.fps_spin), FALSE, FALSE, 0);

    app.brightness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100, 100, 1);
    gtk_widget_set_size_request(app.brightness_scale, 260, -1);
    gtk_scale_set_value_pos(GTK_SCALE(app.brightness_scale), GTK_POS_RIGHT);
    gtk_box_pack_start(GTK_BOX(advanced), row("Brightness", "Darken the wallpaper to make desktop icons and windows stand out.", app.brightness_scale), FALSE, FALSE, 0);

    app.contrast_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100, 100, 1);
    gtk_widget_set_size_request(app.contrast_scale, 260, -1);
    gtk_scale_set_value_pos(GTK_SCALE(app.contrast_scale), GTK_POS_RIGHT);
    gtk_box_pack_start(GTK_BOX(advanced), row("Contrast", "Adjust the difference between dark and bright areas.", app.contrast_scale), FALSE, FALSE, 0);

    app.saturation_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100, 100, 1);
    gtk_widget_set_size_request(app.saturation_scale, 260, -1);
    gtk_scale_set_value_pos(GTK_SCALE(app.saturation_scale), GTK_POS_RIGHT);
    gtk_box_pack_start(GTK_BOX(advanced), row("Saturation", "Reduce color intensity for a calmer wallpaper, or increase it for stronger colors.", app.saturation_scale), FALSE, FALSE, 0);


    GtkWidget *effects_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(effects_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *effects = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(effects), 10);
    gtk_container_add(GTK_CONTAINER(effects_scroll), effects);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), effects_scroll, gtk_label_new("Effects"));

    GtkWidget *effects_intro = gtk_label_new(
        "GPU effects are applied by mpv shaders. For stability, only one effect can be active at a time.");
    gtk_label_set_xalign(GTK_LABEL(effects_intro), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(effects_intro), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(effects_intro), "dim-label");
    gtk_widget_set_margin_top(effects_intro, 10);
    gtk_widget_set_margin_bottom(effects_intro, 10);
    gtk_box_pack_start(GTK_BOX(effects), effects_intro, FALSE, FALSE, 0);

    app.effects=discover_effects();
    if(!app.effects||!app.effects->len){
        GtkWidget *none=gtk_label_new("No effects were found.");gtk_box_pack_start(GTK_BOX(effects),none,FALSE,FALSE,0);
    }else{
        for(guint i=0;i<app.effects->len;i++){
            EffectDef *e=g_ptr_array_index(app.effects,i);EffectParam *a=effect_activation_param(e);if(!a)continue;
            a->slider=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,a->min,a->max,a->step);
            gtk_widget_set_size_request(a->slider,260,-1);gtk_scale_set_digits(GTK_SCALE(a->slider),a->digits);gtk_scale_set_value_pos(GTK_SCALE(a->slider),GTK_POS_RIGHT);gtk_range_set_value(GTK_RANGE(a->slider),a->default_value);
            GtkWidget *expander = NULL;
            if (e->params->len > 1) {
                expander = gtk_expander_new("Parameters");
                gtk_expander_set_expanded(GTK_EXPANDER(expander), FALSE);
                gtk_widget_set_margin_start(expander, 58);
                gtk_widget_set_margin_end(expander, 4);
            }

            GtkWidget *activation_control =
                gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            a->audio_badge = gtk_label_new("Audio controlled");
            gtk_style_context_add_class(
                gtk_widget_get_style_context(a->audio_badge), "dim-label");
            gtk_widget_set_no_show_all(a->audio_badge, TRUE);
            gtk_widget_hide(a->audio_badge);
            gtk_box_pack_start(GTK_BOX(activation_control),
                               a->audio_badge, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(activation_control),
                               a->slider, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(effects),
                               effect_row(e, activation_control, expander),
                               FALSE, FALSE, 0);
            g_signal_connect(a->slider,"value-changed",G_CALLBACK(on_effect_changed),&app);

            if (expander) {
                gtk_box_pack_start(GTK_BOX(effects), expander, FALSE, FALSE, 0);

                GtkWidget *param_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
                gtk_widget_set_margin_top(param_box, 6);
                gtk_widget_set_margin_start(param_box, 12);
                gtk_container_add(GTK_CONTAINER(expander), param_box);

                for(guint j=0;j<e->params->len;j++){
                    EffectParam *p=g_ptr_array_index(e->params,j);
                    if(p==a)continue;

                    p->slider=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,p->min,p->max,p->step);
                    gtk_widget_set_size_request(p->slider,260,-1);
                    gtk_scale_set_digits(GTK_SCALE(p->slider),p->digits);
                    gtk_scale_set_value_pos(GTK_SCALE(p->slider),GTK_POS_RIGHT);
                    gtk_range_set_value(GTK_RANGE(p->slider),p->default_value);

                    GtkWidget *param_control =
                        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                    p->audio_badge = gtk_label_new("Audio controlled");
                    gtk_style_context_add_class(
                        gtk_widget_get_style_context(p->audio_badge), "dim-label");
                    gtk_widget_set_no_show_all(p->audio_badge, TRUE);
                    gtk_widget_hide(p->audio_badge);
                    gtk_box_pack_start(GTK_BOX(param_control),
                                       p->audio_badge, FALSE, FALSE, 0);
                    gtk_box_pack_start(GTK_BOX(param_control),
                                       p->slider, FALSE, FALSE, 0);

                    GtkWidget *pr=row(p->name,"",param_control);
                    gtk_box_pack_start(GTK_BOX(param_box),pr,FALSE,FALSE,0);

                    g_signal_connect(p->slider,"value-changed",
                                     G_CALLBACK(on_effect_changed),&app);
                }
            }
        }
    }

    GtkWidget *audio_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(audio_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *audio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(audio), 10);
    gtk_container_add(GTK_CONTAINER(audio_scroll), audio);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), audio_scroll, gtk_label_new("Audio Visualizer"));

    GtkWidget *audio_intro = gtk_label_new(
        "Use the current system audio output to animate a parameter of the active GPU effect. "
        "The selected effect parameter acts as the maximum value; silence moves it toward that parameter's minimum.");
    gtk_label_set_xalign(GTK_LABEL(audio_intro), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(audio_intro), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(audio_intro), "dim-label");
    gtk_box_pack_start(GTK_BOX(audio), audio_intro, FALSE, FALSE, 4);

    GtkWidget *audio_checks = centered_check_group();
    app.audio_enabled_check = gtk_check_button_new_with_label("Enable audio-reactive effect");
    centered_check_group_add(audio_checks, app.audio_enabled_check);
    app.audio_waveform_check = gtk_check_button_new_with_label("Show waveform overlay");
    centered_check_group_add(audio_checks, app.audio_waveform_check);
    gtk_box_pack_start(GTK_BOX(audio), audio_checks, FALSE, FALSE, 0);

    app.audio_effect_label = gtk_label_new("Active effect: None");
    gtk_label_set_xalign(GTK_LABEL(app.audio_effect_label), 0.0);
    gtk_widget_set_margin_top(app.audio_effect_label, 8);
    gtk_box_pack_start(GTK_BOX(audio), app.audio_effect_label, FALSE, FALSE, 0);

    app.audio_device_combo = gtk_combo_box_text_new();
    populate_audio_device_combo(GTK_COMBO_BOX_TEXT(app.audio_device_combo));
    gtk_widget_set_size_request(app.audio_device_combo, 220, -1);
    gtk_widget_set_hexpand(app.audio_device_combo, FALSE);
    gtk_widget_set_halign(app.audio_device_combo, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(audio), row("Audio device",
        "Automatic samples available output monitor sources and follows the one carrying audio. Choose a specific monitor to override it.",
        app.audio_device_combo), FALSE, FALSE, 0);

    app.audio_source_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.audio_source_combo), "Bass");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.audio_source_combo), "Overall level");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.audio_source_combo), 0);
    gtk_widget_set_size_request(app.audio_source_combo, 220, -1);
    gtk_box_pack_start(GTK_BOX(audio), row("Audio source",
        "Bass follows roughly 40â€“180 Hz and usually gives stronger rhythmic variation than overall volume.",
        app.audio_source_combo), FALSE, FALSE, 0);

    app.audio_parameter_combo = gtk_combo_box_text_new();
    gtk_widget_set_size_request(app.audio_parameter_combo, 220, -1);
    gtk_box_pack_start(GTK_BOX(audio), row("Controlled parameter",
        "Parameters are taken automatically from the currently active effect.",
        app.audio_parameter_combo), FALSE, FALSE, 0);

    app.audio_sensitivity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 10.0, 0.1);
    gtk_widget_set_size_request(app.audio_sensitivity_scale, 260, -1);
    gtk_scale_set_digits(GTK_SCALE(app.audio_sensitivity_scale), 1);
    gtk_scale_set_value_pos(GTK_SCALE(app.audio_sensitivity_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(app.audio_sensitivity_scale), 2.0);
    gtk_box_pack_start(GTK_BOX(audio), row("Sensitivity",
        "How strongly the selected audio signal drives the selected parameter.",
        app.audio_sensitivity_scale), FALSE, FALSE, 0);

    app.audio_smoothing_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 0.98, 0.01);
    gtk_widget_set_size_request(app.audio_smoothing_scale, 260, -1);
    gtk_scale_set_digits(GTK_SCALE(app.audio_smoothing_scale), 2);
    gtk_scale_set_value_pos(GTK_SCALE(app.audio_smoothing_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(app.audio_smoothing_scale), 0.82);
    gtk_box_pack_start(GTK_BOX(audio), row("Smoothing",
        "Higher values produce slower, softer movement; lower values react more sharply to beats.",
        app.audio_smoothing_scale), FALSE, FALSE, 0);

    refresh_audio_effect_controls(&app);

    GtkWidget *presets = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(presets), 10);
    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), presets, gtk_label_new("Presets"));

    GtkWidget *preset_intro = gtk_label_new(
        "Save and load Effects and Audio Visualizer settings. "
        "Loading a preset changes the controls but does not apply them to "
        "the desktop until Set Wallpaper is pressed.");
    gtk_label_set_xalign(GTK_LABEL(preset_intro), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(preset_intro), TRUE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(preset_intro), "dim-label");
    gtk_box_pack_start(GTK_BOX(presets), preset_intro, FALSE, FALSE, 0);

    GtkWidget *preset_content =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(
        GTK_BOX(presets), preset_content, TRUE, TRUE, 0);

    GtkWidget *preset_list_scroll =
        gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(preset_list_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(preset_list_scroll, 220, 280);

    app.preset_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(
        GTK_LIST_BOX(app.preset_list), GTK_SELECTION_SINGLE);
    gtk_container_add(
        GTK_CONTAINER(preset_list_scroll), app.preset_list);
    gtk_box_pack_start(
        GTK_BOX(preset_content),
        preset_list_scroll, FALSE, FALSE, 0);

    GtkWidget *values_box =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(
        GTK_BOX(preset_content), values_box, TRUE, TRUE, 0);

    GtkWidget *values_label = gtk_label_new("Preset values");
    gtk_label_set_xalign(GTK_LABEL(values_label), 0.0);
    gtk_box_pack_start(
        GTK_BOX(values_box), values_label, FALSE, FALSE, 0);

    GtkWidget *values_scroll =
        gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(values_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(
        GTK_BOX(values_box), values_scroll, TRUE, TRUE, 0);

    app.preset_values_view = gtk_text_view_new();
    gtk_text_view_set_editable(
        GTK_TEXT_VIEW(app.preset_values_view), FALSE);
    gtk_text_view_set_cursor_visible(
        GTK_TEXT_VIEW(app.preset_values_view), FALSE);
    gtk_text_view_set_wrap_mode(
        GTK_TEXT_VIEW(app.preset_values_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(
        GTK_CONTAINER(values_scroll), app.preset_values_view);

    GtkWidget *preset_buttons =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(
        GTK_BOX(presets), preset_buttons, FALSE, FALSE, 0);

    GtkWidget *save_preset_button =
        gtk_button_new_with_label("Save new preset");
    gtk_box_pack_start(
        GTK_BOX(preset_buttons),
        save_preset_button, FALSE, FALSE, 0);

    app.preset_load_button =
        gtk_button_new_with_label("Load preset");
    gtk_widget_set_sensitive(app.preset_load_button, FALSE);
    gtk_box_pack_start(
        GTK_BOX(preset_buttons),
        app.preset_load_button, FALSE, FALSE, 0);

    app.preset_delete_button =
        gtk_button_new_with_label("Delete preset");
    gtk_widget_set_sensitive(app.preset_delete_button, FALSE);
    gtk_box_pack_start(
        GTK_BOX(preset_buttons),
        app.preset_delete_button, FALSE, FALSE, 0);

    g_signal_connect(
        app.preset_list, "row-selected",
        G_CALLBACK(on_preset_selected), &app);
    g_signal_connect(
        save_preset_button, "clicked",
        G_CALLBACK(on_save_preset_clicked), &app);
    g_signal_connect(
        app.preset_load_button, "clicked",
        G_CALLBACK(on_load_preset_clicked), &app);
    g_signal_connect(
        app.preset_delete_button, "clicked",
        G_CALLBACK(on_delete_preset_clicked), &app);

    refresh_presets(&app);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root), sep, FALSE, FALSE, 2);

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    app.status_indicator = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.status_indicator, 14, 14);
    g_signal_connect(app.status_indicator, "draw", G_CALLBACK(draw_status_indicator), &app);
    gtk_box_pack_start(GTK_BOX(status_box), app.status_indicator, FALSE, FALSE, 0);
    app.status_label = gtk_label_new("Using Xfce desktop background");
    gtk_label_set_xalign(GTK_LABEL(app.status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(status_box), app.status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bottom), status_box, TRUE, TRUE, 0);

    GtkWidget *reset_defaults_button = gtk_button_new_with_label("Reset to Defaults");
    gtk_widget_set_tooltip_text(reset_defaults_button, "Restore playback and advanced settings to their defaults. The selected wallpaper and autostart choice are kept. Changes are not applied until Set Wallpaper is pressed.");
    gtk_box_pack_end(GTK_BOX(bottom), reset_defaults_button, FALSE, FALSE, 0);

    app.turn_off_button = gtk_button_new_with_label("Turn Off");
    gtk_widget_set_tooltip_text(app.turn_off_button, "Stop the animated wallpaper and reveal the wallpaper configured in XFCE.");
    gtk_widget_set_sensitive(app.turn_off_button, FALSE);
    gtk_box_pack_end(GTK_BOX(bottom), app.turn_off_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), bottom, FALSE, FALSE, 0);

    GtkWidget *set_wallpaper = gtk_button_new_with_label("Set Wallpaper");
    app.set_wallpaper_button = set_wallpaper;
    gtk_style_context_add_class(gtk_widget_get_style_context(set_wallpaper), "suggested-action");
    gtk_widget_set_hexpand(set_wallpaper, TRUE);
    gtk_widget_set_size_request(set_wallpaper, -1, 44);
    gtk_box_pack_start(GTK_BOX(root), set_wallpaper, FALSE, FALSE, 0);

    g_signal_connect(app.preview_area, "realize", G_CALLBACK(on_preview_realize), &app);
    g_signal_connect(app.preview_area, "plug-removed", G_CALLBACK(on_preview_plug_removed), &app);
    g_signal_connect(app.preview_eventbox, "button-press-event", G_CALLBACK(on_preview_clicked), &app);
    g_signal_connect(app.preview_toggle_button, "clicked",
                     G_CALLBACK(on_preview_toggle_clicked), &app);
    gtk_widget_add_events(app.preview_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(app.preview_area, "button-press-event", G_CALLBACK(on_preview_clicked), &app);
    g_signal_connect(app.source_local, "toggled", G_CALLBACK(on_source_toggled), &app);
    g_signal_connect(app.source_stream, "toggled", G_CALLBACK(on_source_toggled), &app);
    g_signal_connect(app.stream_entry, "changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.reconnect_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(gallery_button, "clicked", G_CALLBACK(on_gallery_clicked), &app);
    g_signal_connect(app.file_button, "file-set", G_CALLBACK(on_file_set), &app);
    g_signal_connect(app.mode_fill, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.mode_fit, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.mode_stretch, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.speed_scale, "value-changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.mute_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.loop_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.hwdec_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.fps_spin, "value-changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.autostart_check, "toggled", G_CALLBACK(on_autostart_toggled), &app);
    g_signal_connect(app.desktop_icons_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.audio_enabled_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.audio_waveform_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.audio_source_combo, "changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.audio_device_combo, "changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.audio_parameter_combo, "changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.audio_sensitivity_scale, "value-changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.audio_smoothing_scale, "value-changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.interpolation_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.pause_fullscreen_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.pause_battery_check, "toggled", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.brightness_scale, "value-changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.contrast_scale, "value-changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(app.saturation_scale, "value-changed", G_CALLBACK(on_setting_changed), &app);
    g_signal_connect(reset_defaults_button, "clicked", G_CALLBACK(on_reset_clicked), &app);
    g_signal_connect(set_wallpaper, "clicked", G_CALLBACK(on_set_clicked), &app);
    g_signal_connect(app.turn_off_button, "clicked", G_CALLBACK(on_turn_off_clicked), &app);

    load_config(&app);
    gtk_widget_show_all(app.window);
    update_audio_control_indicators(&app);
    schedule_preview_aspect_update(&app);
    app.status_poll_source = g_timeout_add_seconds(2, status_poll_cb, &app);
    update_source_controls(&app);

    /*
     * load_config() runs before widgets are realized.  Starting the preview
     * there can therefore see no native X11 window yet.  Queue an explicit
     * retry after the complete settings window has been mapped instead of
     * relying solely on GtkSocket::realize.
     */
    app.preview_restart_source = g_timeout_add(100, restart_preview_cb, &app);

    gtk_main();
    return 0;
}

#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

static void stop_audio_visualizer(void);

typedef struct {gchar *id,*placeholder;gdouble min,max,default_value;gint order;} BackendParam;
typedef struct {gchar *id,*shader_path;gint order;GPtrArray *params;} BackendEffect;
static void backend_param_free(gpointer d){BackendParam*p=d;if(!p)return;g_free(p->id);g_free(p->placeholder);g_free(p);}
static void backend_effect_free(gpointer d){BackendEffect*e=d;if(!e)return;g_free(e->id);g_free(e->shader_path);if(e->params)g_ptr_array_free(e->params,TRUE);g_free(e);}
static gboolean backend_effect_loaded(GPtrArray*a,const gchar*id){for(guint i=0;a&&i<a->len;i++){BackendEffect*e=g_ptr_array_index(a,i);if(g_strcmp0(e->id,id)==0)return TRUE;}return FALSE;}
static gint backend_effect_sort(gconstpointer a,gconstpointer b){const BackendEffect*ea=*(BackendEffect*const*)a,*eb=*(BackendEffect*const*)b;return ea->order!=eb->order?ea->order-eb->order:g_strcmp0(ea->id,eb->id);}
static gint backend_param_sort(gconstpointer a,gconstpointer b){const BackendParam*pa=*(BackendParam*const*)a,*pb=*(BackendParam*const*)b;return pa->order-pb->order;}
static BackendParam*backend_activation_param(BackendEffect*e){if(!e||!e->params||!e->params->len)return NULL;for(guint i=0;i<e->params->len;i++){BackendParam*p=g_ptr_array_index(e->params,i);if(g_strcmp0(p->id,"strength")==0)return p;}return g_ptr_array_index(e->params,0);}
static BackendEffect*backend_effect_by_id(GPtrArray*a,const gchar*id){if(!id||!*id)return NULL;for(guint i=0;a&&i<a->len;i++){BackendEffect*e=g_ptr_array_index(a,i);if(g_strcmp0(e->id,id)==0)return e;}return NULL;}
static void backend_load_params(GKeyFile*k,BackendEffect*e){gsize n=0;gchar**g=g_key_file_get_groups(k,&n);for(gsize i=0;g&&i<n;i++){if(!g_str_has_prefix(g[i],"Parameter "))continue;const gchar*id=g[i]+strlen("Parameter ");if(!*id)continue;BackendParam*p=g_new0(BackendParam,1);p->id=g_strdup(id);p->placeholder=g_key_file_has_key(k,g[i],"placeholder",NULL)?g_key_file_get_string(k,g[i],"placeholder",NULL):g_ascii_strup(id,-1);p->min=g_key_file_has_key(k,g[i],"min",NULL)?g_key_file_get_double(k,g[i],"min",NULL):0;p->max=g_key_file_has_key(k,g[i],"max",NULL)?g_key_file_get_double(k,g[i],"max",NULL):100;p->default_value=g_key_file_has_key(k,g[i],"default",NULL)?g_key_file_get_double(k,g[i],"default",NULL):p->min;p->order=g_key_file_has_key(k,g[i],"order",NULL)?g_key_file_get_integer(k,g[i],"order",NULL):1000;g_ptr_array_add(e->params,p);}g_strfreev(g);if(!e->params->len){BackendParam*p=g_new0(BackendParam,1);p->id=g_strdup("strength");p->placeholder=g_strdup("VALUE");p->min=g_key_file_has_key(k,"Effect","min",NULL)?g_key_file_get_double(k,"Effect","min",NULL):0;p->max=g_key_file_has_key(k,"Effect","max",NULL)?g_key_file_get_double(k,"Effect","max",NULL):100;p->default_value=g_key_file_has_key(k,"Effect","default",NULL)?g_key_file_get_double(k,"Effect","default",NULL):p->min;g_ptr_array_add(e->params,p);}g_ptr_array_sort(e->params,backend_param_sort);}
static void backend_load_effect_dir(GPtrArray*a,const gchar*base){if(!base||!g_file_test(base,G_FILE_TEST_IS_DIR))return;GDir*d=g_dir_open(base,0,NULL);if(!d)return;const gchar*n;while((n=g_dir_read_name(d))){gchar*f=g_build_filename(base,n,NULL),*m=g_build_filename(f,"effect.ini",NULL);if(!g_file_test(m,G_FILE_TEST_IS_REGULAR)){g_free(m);g_free(f);continue;}GKeyFile*k=g_key_file_new();if(!g_key_file_load_from_file(k,m,G_KEY_FILE_NONE,NULL)){g_key_file_unref(k);g_free(m);g_free(f);continue;}gchar*id=g_key_file_get_string(k,"Effect","id",NULL),*sn=g_key_file_get_string(k,"Effect","shader",NULL);if(!id||!*id||!sn||!*sn||backend_effect_loaded(a,id)){g_free(id);g_free(sn);g_key_file_unref(k);g_free(m);g_free(f);continue;}gchar*sp=g_build_filename(f,sn,NULL);if(!g_file_test(sp,G_FILE_TEST_IS_REGULAR)){g_free(sp);g_free(id);g_free(sn);g_key_file_unref(k);g_free(m);g_free(f);continue;}BackendEffect*e=g_new0(BackendEffect,1);e->id=id;e->shader_path=sp;e->order=g_key_file_has_key(k,"Effect","order",NULL)?g_key_file_get_integer(k,"Effect","order",NULL):1000;e->params=g_ptr_array_new_with_free_func(backend_param_free);backend_load_params(k,e);g_ptr_array_add(a,e);g_free(sn);g_key_file_unref(k);g_free(m);g_free(f);}g_dir_close(d);}
static GPtrArray*backend_discover_effects(void){GPtrArray*a=g_ptr_array_new_with_free_func(backend_effect_free);gchar*u=g_build_filename(g_get_user_data_dir(),"xfce-animated-wallpaper","effects",NULL);backend_load_effect_dir(a,u);g_free(u);backend_load_effect_dir(a,"./effects");backend_load_effect_dir(a,"/usr/local/share/xfce-animated-wallpaper/effects");backend_load_effect_dir(a,"/usr/share/xfce-animated-wallpaper/effects");g_ptr_array_sort(a,backend_effect_sort);return a;}


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

static gchar *config_path(void) {
    return g_build_filename(g_get_user_config_dir(), "xfce-animated-wallpaper", "config.ini", NULL);
}

static gchar *pid_path(void) {
    return g_build_filename(g_get_user_runtime_dir() ? g_get_user_runtime_dir() : "/tmp",
                            "xfce-animated-wallpaper.pid", NULL);
}

static gchar *icons_pid_path(void) {
    return g_build_filename(g_get_user_runtime_dir() ? g_get_user_runtime_dir() : "/tmp",
                            "xfce-animated-wallpaper-icons.pid", NULL);
}

static GPid read_icons_pid(void) {
    gchar *path = icons_pid_path();
    gchar *contents = NULL;
    GPid pid = 0;
    if (g_file_get_contents(path, &contents, NULL, NULL) && contents)
        pid = (GPid)g_ascii_strtoll(contents, NULL, 10);
    g_free(contents);
    g_free(path);
    return pid;
}

static void remove_icons_pidfile(void) {
    gchar *path = icons_pid_path();
    g_unlink(path);
    g_free(path);
}

static gboolean write_icons_pid(GPid pid, GError **error) {
    gchar *path = icons_pid_path();
    gchar *text = g_strdup_printf("%d\n", (int)pid);
    gboolean ok = g_file_set_contents(path, text, -1, error);
    g_free(text);
    g_free(path);
    return ok;
}

static gchar *wallpaper_log_path(void) {
    gchar *dir = g_build_filename(g_get_user_cache_dir(), "xfce-animated-wallpaper", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *path = g_build_filename(dir, "wallpaper-mpv.log", NULL);
    g_free(dir);
    return path;
}


static gboolean process_alive(GPid pid) {
    return pid > 1 && kill(-pid, 0) == 0;
}

static void child_setup(gpointer data) {
    (void)data;
    setpgid(0, 0);
}

static gboolean stop_desktop_icons(void) {
    GPid pid = read_icons_pid();
    if (pid <= 1 || kill(-pid, 0) != 0) {
        remove_icons_pidfile();
        return TRUE;
    }

    kill(-pid, SIGTERM);
    for (int i = 0; i < 10; i++) {
        g_usleep(50000);
        if (kill(-pid, 0) != 0) {
            remove_icons_pidfile();
            return TRUE;
        }
    }

    kill(-pid, SIGKILL);
    g_usleep(50000);
    remove_icons_pidfile();
    return kill(-pid, 0) != 0;
}

static gboolean start_desktop_icons(void) {
    stop_desktop_icons();

    gchar *argv[] = { (gchar *)"xfce-animated-wallpaper-icons", NULL };
    GPid pid = 0;
    GError *error = NULL;

    gboolean ok = g_spawn_async(
        NULL, argv, NULL,
        G_SPAWN_SEARCH_PATH |
        G_SPAWN_DO_NOT_REAP_CHILD |
        G_SPAWN_STDOUT_TO_DEV_NULL |
        G_SPAWN_STDERR_TO_DEV_NULL,
        child_setup, NULL, &pid, &error);

    if (!ok) {
        g_printerr("Could not start desktop icon layer: %s\n",
                   error ? error->message : "unknown error");
        g_clear_error(&error);
        return FALSE;
    }

    if (!write_icons_pid(pid, &error)) {
        g_printerr("Desktop icons started, but PID file could not be written: %s\n",
                   error ? error->message : "unknown error");
        g_clear_error(&error);
    }

    return TRUE;
}

static GPid read_pid(void) {
    gchar *path = pid_path();
    gchar *contents = NULL;
    GPid pid = 0;
    if (g_file_get_contents(path, &contents, NULL, NULL) && contents) {
        pid = (GPid)g_ascii_strtoll(contents, NULL, 10);
    }
    g_free(contents);
    g_free(path);
    return pid;
}

static void remove_pidfile(void) {
    gchar *path = pid_path();
    g_unlink(path);
    g_free(path);
}

static gboolean stop_wallpaper(void) {
    stop_audio_visualizer();
    stop_desktop_icons();

    GPid pid = read_pid();
    if (!process_alive(pid)) {
        remove_pidfile();
        return TRUE;
    }

    kill(-pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
        g_usleep(100000);
        if (!process_alive(pid)) {
            remove_pidfile();
            return TRUE;
        }
    }
    kill(-pid, SIGKILL);
    g_usleep(100000);
    remove_pidfile();
    return !process_alive(pid);
}

static gboolean write_pid(GPid pid, GError **error) {
    gchar *path = pid_path();
    gchar *text = g_strdup_printf("%d\n", (int)pid);
    gboolean ok = g_file_set_contents(path, text, -1, error);
    g_free(text);
    g_free(path);
    return ok;
}

static gchar *get_string(GKeyFile *kf, const gchar *group, const gchar *key, const gchar *fallback) {
    GError *err = NULL;
    gchar *v = g_key_file_get_string(kf, group, key, &err);
    if (err) {
        g_clear_error(&err);
        return g_strdup(fallback);
    }
    return v;
}

static gboolean get_bool(GKeyFile *kf, const gchar *group, const gchar *key, gboolean fallback) {
    GError *err = NULL;
    gboolean v = g_key_file_get_boolean(kf, group, key, &err);
    if (err) {
        g_clear_error(&err);
        return fallback;
    }
    return v;
}

static gdouble get_double(GKeyFile *kf, const gchar *group, const gchar *key, gdouble fallback) {
    GError *err = NULL;
    gdouble v = g_key_file_get_double(kf, group, key, &err);
    if (err) {
        g_clear_error(&err);
        return fallback;
    }
    return v;
}


static gboolean xfdesktop_running(void) {
    gchar *out = NULL;
    gint status = 0;
    GError *err = NULL;
    gboolean ok = g_spawn_command_line_sync(
        "pgrep -x xfdesktop", &out, NULL, &status, &err);
    if (err) g_clear_error(&err);
    gboolean running = ok && status == 0 && out && *out;
    g_free(out);
    return running;
}

static gboolean wait_for_safe_desktop(void) {
    const gchar *display = g_getenv("DISPLAY");
    const gchar *session = g_getenv("XDG_SESSION_TYPE");

    if (!display || !*display) {
        g_printerr("Autostart skipped: no X11 DISPLAY is available.\n");
        return FALSE;
    }
    if (session && *session && g_ascii_strcasecmp(session, "x11") != 0) {
        g_printerr("Autostart skipped: animated wallpaper requires an X11 session.\n");
        return FALSE;
    }

    /*
     * Starting xwinwrap before xfdesktop has mapped the desktop can leave the
     * wrapper as an ordinary fullscreen window above the session. Wait for
     * xfdesktop instead, and fail closed if it never appears.
     */
    for (int i = 0; i < 30; i++) {
        if (xfdesktop_running()) {
            /* Give xfdesktop a little extra time to finish mapping its window. */
            g_usleep(2000000);
            return TRUE;
        }
        g_usleep(500000);
    }

    g_printerr("Autostart skipped: xfdesktop did not become ready.\n");
    return FALSE;
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



static gdouble backend_param_value(GKeyFile *k, BackendEffect *e, BackendParam *p) {
    gchar *g = g_strdup_printf("effect.%s", e->id);
    gdouble v = p->default_value;
    if (g_key_file_has_key(k, g, p->id, NULL))
        v = g_key_file_get_double(k, g, p->id, NULL);
    else if (p == backend_activation_param(e) && g_key_file_has_key(k, "effects", e->id, NULL))
        v = g_key_file_get_double(k, "effects", e->id, NULL);
    g_free(g);
    return v;
}

static gchar *materialize_effect_shader(GKeyFile *k, BackendEffect *e) {
    gchar *r = NULL;
    if (!e || !g_file_get_contents(e->shader_path, &r, NULL, NULL))
        return NULL;

    gboolean audio_enabled = g_key_file_has_key(k, "audio", "enabled", NULL)
        ? g_key_file_get_boolean(k, "audio", "enabled", NULL) : FALSE;
    gchar *audio_parameter = g_key_file_has_key(k, "audio", "parameter", NULL)
        ? g_key_file_get_string(k, "audio", "parameter", NULL) : g_strdup("strength");
    gboolean uses_audio = FALSE;

    for (guint i = 0; i < e->params->len; i++) {
        BackendParam *p = g_ptr_array_index(e->params, i);
        gdouble base = backend_param_value(k, e, p);
        gchar value[G_ASCII_DTOSTR_BUF_SIZE];
        gchar *replacement = NULL;

        if (audio_enabled && g_strcmp0(p->id, audio_parameter) == 0) {
            gchar minv[G_ASCII_DTOSTR_BUF_SIZE];
            gchar basev[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(minv, sizeof minv, "%.6f", p->min);
            g_ascii_formatd(basev, sizeof basev, "%.6f", base);
            replacement = g_strdup_printf("mix(%s, %s, aw_audio)", minv, basev);
            uses_audio = TRUE;
        } else {
            g_ascii_formatd(value, sizeof value, "%.6f", base);
            replacement = g_strdup(value);
        }

        gchar *token = g_strdup_printf("@%s@", p->placeholder);
        gchar **parts = g_strsplit(r, token, -1);
        gchar *next = g_strjoinv(replacement, parts);
        g_strfreev(parts);
        g_free(token);
        g_free(replacement);
        g_free(r);
        r = next;
    }

    if (uses_audio) {
        const gchar *param_block =
            "//!PARAM aw_audio\n"
            "//!DESC Live audio level\n"
            "//!TYPE DYNAMIC float\n"
            "//!MINIMUM 0.0\n"
            "//!MAXIMUM 1.0\n"
            "0.0\n\n";
        gchar *next = g_strconcat(param_block, r, NULL);
        g_free(r);
        r = next;
    }

    g_free(audio_parameter);

    gchar *d = g_build_filename(g_get_user_cache_dir(), "xfce-animated-wallpaper", "shaders", NULL);
    g_mkdir_with_parents(d, 0700);
    gchar *n = g_strdup_printf("%s-generated.glsl", e->id);
    gchar *path = g_build_filename(d, n, NULL);
    if (!g_file_set_contents(path, r, -1, NULL)) {
        g_free(path);
        path = NULL;
    }
    g_free(n); g_free(d); g_free(r);
    return path;
}

static void add_backend_effect_shader(GPtrArray *a, GKeyFile *k, BackendEffect *e) {
    gchar *p = materialize_effect_shader(k, e);
    if (!p) return;
    g_ptr_array_add(a, g_strdup_printf("--glsl-shader=%s", p));
    g_free(p);
}

static gchar *audio_ipc_path(void) {
    const gchar *runtime = g_get_user_runtime_dir();
    return g_build_filename(runtime ? runtime : "/tmp", "xfce-animated-wallpaper-mpv.sock", NULL);
}

static gchar *visualizer_pid_path(void) {
    const gchar *runtime = g_get_user_runtime_dir();
    return g_build_filename(runtime ? runtime : "/tmp", "xfce-animated-wallpaper-visualizer.pid", NULL);
}

static GPid read_visualizer_pid(void) {
    gchar *path = visualizer_pid_path();
    gchar *text = NULL;
    GPid pid = 0;
    if (g_file_get_contents(path, &text, NULL, NULL) && text)
        pid = (GPid)g_ascii_strtoll(text, NULL, 10);
    g_free(text); g_free(path);
    return pid;
}

static void stop_audio_visualizer(void) {
    GPid pid = read_visualizer_pid();
    if (pid > 1) {
        kill(pid, SIGTERM);
        g_usleep(50000);
    }
    gchar *pidfile = visualizer_pid_path();
    g_unlink(pidfile);
    g_free(pidfile);
    gchar *ipc = audio_ipc_path();
    g_unlink(ipc);
    g_free(ipc);
}

static gboolean start_audio_visualizer(void) {
    GPid old_pid = read_visualizer_pid();
    if (old_pid > 1) {
        kill(old_pid, SIGTERM);
        g_usleep(50000);
    }
    gchar *old_pidfile = visualizer_pid_path();
    g_unlink(old_pidfile);
    g_free(old_pidfile);

    gchar *argv[] = {(gchar *)"xfce-animated-wallpaper-visualizer", (gchar *)"--control", NULL};
    GPid pid = 0;
    GError *error = NULL;
    gboolean ok = g_spawn_async(NULL, argv, NULL,
                                G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                                G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                                NULL, NULL, &pid, &error);
    if (!ok) {
        g_printerr("Could not start audio visualizer: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        return FALSE;
    }
    gchar *pidfile = visualizer_pid_path();
    gchar *text = g_strdup_printf("%d\n", (int)pid);
    g_file_set_contents(pidfile, text, -1, NULL);
    g_free(text); g_free(pidfile);
    return TRUE;
}

static gboolean start_wallpaper(gboolean require_enabled) {
    gchar *cfg = config_path();
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;

    if (!g_key_file_load_from_file(kf, cfg, G_KEY_FILE_NONE, &err)) {
        g_printerr("Could not read %s: %s\n", cfg, err->message);
        g_clear_error(&err);
        g_key_file_unref(kf);
        g_free(cfg);
        return FALSE;
    }

    gboolean enabled = get_bool(kf, "wallpaper", "enabled", FALSE);
    if (require_enabled && !enabled) {
        g_key_file_unref(kf);
        g_free(cfg);
        return TRUE;
    }

    gchar *source = get_string(kf, "wallpaper", "applied_source", "");
    gchar *video = get_string(kf, "wallpaper", "applied_video", "");
    gchar *stream_url = get_string(kf, "wallpaper", "applied_stream", "");

    /* Backward compatibility with configs written before applied_* existed. */
    if (!source || !*source) {
        g_free(source);
        source = get_string(kf, "wallpaper", "source", "local");
    }
    if ((!video || !*video) && g_strcmp0(source, "local") == 0) {
        g_free(video);
        video = get_string(kf, "wallpaper", "video", "");
    }
    if ((!stream_url || !*stream_url) && g_strcmp0(source, "stream") == 0) {
        g_free(stream_url);
        stream_url = get_string(kf, "wallpaper", "stream_url", "");
    }

    gboolean reconnect = get_bool(kf, "wallpaper", "reconnect", TRUE);
    gchar *mode = get_string(kf, "wallpaper", "mode", "fill");
    gboolean mute = get_bool(kf, "playback", "mute", TRUE);
    gboolean loop = get_bool(kf, "playback", "loop", TRUE);
    gboolean hwdec = get_bool(kf, "playback", "hwdec", TRUE);
    gdouble speed = get_double(kf, "playback", "speed", 1.0);
    gint fps = (gint)get_double(kf, "playback", "fps_limit", 0.0);
    gboolean interpolation = get_bool(kf, "advanced", "interpolation", FALSE);
    gboolean pause_fullscreen = get_bool(kf, "advanced", "pause_fullscreen", TRUE);
    gboolean pause_battery = get_bool(kf, "advanced", "pause_battery", FALSE);
    gboolean desktop_icons = get_bool(kf, "desktop", "show_icons", FALSE);
    gboolean audio_enabled = get_bool(kf, "audio", "enabled", FALSE);
    gdouble brightness = get_double(kf, "advanced", "brightness", 0.0);
    gdouble contrast = get_double(kf, "advanced", "contrast", 0.0);
    gdouble saturation = get_double(kf, "advanced", "saturation", 0.0);

    gboolean is_stream = g_strcmp0(source, "stream") == 0;
    const gchar *media = is_stream ? stream_url : video;
    gboolean restart_web_stream =
        is_stream && !url_is_direct_stream(media) && reconnect;

    if (!media || !*media ||
        (!is_stream && !g_file_test(media, G_FILE_TEST_IS_REGULAR))) {
        g_printerr(is_stream
                       ? "No valid wallpaper stream configured.\n"
                       : "No valid wallpaper video configured.\n");
        g_free(source); g_free(video); g_free(stream_url); g_free(mode);
        g_key_file_unref(kf); g_free(cfg);
        return FALSE;
    }

    stop_wallpaper();

    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);

    /*
     * xwinwrap replaces WID only when it is a standalone argument, while
     * modern mpv requires --wid=<value>.  Put a tiny /bin/sh adapter between
     * them: xwinwrap substitutes WID, then the shell rebuilds mpv's required
     * --wid= form and forwards every remaining argument unchanged.
     *
     * This mirrors the known-good command:
     *   xwinwrap -ov -fs -ni -b -nf -- \
     *     sh -c 'wid=$1; shift; exec mpv --wid="$wid" "$@"' sh WID ...
     */
    gchar *wall_log = wallpaper_log_path();

    /*
     * Debian packages bundle xwinwrap privately so users do not need to
     * install it separately. Source/manual installs can still use xwinwrap
     * from PATH.
     */
    const gchar *bundled_xwinwrap =
        "/usr/lib/xfce-animated-wallpaper/xwinwrap";
    gchar *xwinwrap_path = NULL;

    if (g_file_test(bundled_xwinwrap, G_FILE_TEST_IS_EXECUTABLE))
        xwinwrap_path = g_strdup(bundled_xwinwrap);
    else
        xwinwrap_path = g_find_program_in_path("xwinwrap");

    if (!xwinwrap_path) {
        g_printerr(
            "xwinwrap was not found. Install xwinwrap or use the packaged build.\n");
        g_ptr_array_free(argv, TRUE);
        g_free(wall_log);
        g_free(source);
        g_free(video);
        g_free(stream_url);
        g_free(mode);
        g_key_file_unref(kf);
        g_free(cfg);
        return FALSE;
    }

    const gchar *xw_args[] = {
        xwinwrap_path, "-ov", "-fs", "-ni", "-b", "-nf", "--",
        "sh", "-c",
        "wid=$1; pf=$2; pb=$3; wr=$4; log=$5; shift 5; "
        ": >\"$log\"; "
        "while :; do "
        "mpv --wid=\"$wid\" \"$@\" >>\"$log\" 2>&1 & p=$!; paused=0; "
        "while kill -0 $p 2>/dev/null; do want=0; "
        "if [ \"$pf\" = 1 ]; then "
        "aw=$(xprop -root _NET_ACTIVE_WINDOW 2>/dev/null | awk '{print $5}'); "
        "if [ -n \"$aw\" ] && [ \"$aw\" != 0x0 ] && xprop -id \"$aw\" _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_FULLSCREEN; then want=1; fi; fi; "
        "if [ \"$pb\" = 1 ]; then for st in /sys/class/power_supply/*/status; do "
        "[ -r \"$st\" ] && grep -qx Discharging \"$st\" && want=1; done; fi; "
        "if [ $want -eq 1 ] && [ $paused -eq 0 ]; then kill -STOP $p 2>/dev/null; paused=1; "
        "elif [ $want -eq 0 ] && [ $paused -eq 1 ]; then kill -CONT $p 2>/dev/null; paused=0; fi; sleep 2; done; "
        "wait $p; rc=$?; "
        "if [ \"$wr\" != 1 ]; then exit $rc; fi; "
        "sleep 2; "
        "done",
        "sh", "WID",
        pause_fullscreen ? "1" : "0", pause_battery ? "1" : "0",
        restart_web_stream ? "1" : "0",
        wall_log,
        NULL
    };
    for (int i = 0; xw_args[i]; i++)
        g_ptr_array_add(argv, g_strdup(xw_args[i]));

    g_ptr_array_add(argv, g_strdup("--really-quiet"));
    g_ptr_array_add(argv, g_strdup("--no-osc"));
    g_ptr_array_add(argv, g_strdup("--no-input-default-bindings"));
    g_ptr_array_add(argv, g_strdup("--no-border"));
    g_ptr_array_add(argv, g_strdup("--framedrop=vo"));
    g_ptr_array_add(argv, g_strdup("--vo=gpu-next"));

    gchar *ipc_path = NULL;
    if (audio_enabled) {
        ipc_path = audio_ipc_path();
        g_unlink(ipc_path);
        g_ptr_array_add(argv, g_strdup_printf("--input-ipc-server=%s", ipc_path));
    }

    if (mute) g_ptr_array_add(argv, g_strdup("--no-audio"));
    gchar *play_media = NULL;
    gchar *static_cache = NULL;

    if (!is_stream && path_is_static_image(media)) {
        GError *cache_err = NULL;
        static_cache = static_image_cache_video(media, &cache_err);
        if (!static_cache) {
            g_printerr("Could not prepare static wallpaper: %s\n",
                       cache_err ? cache_err->message : "unknown error");
            g_clear_error(&cache_err);
            g_ptr_array_free(argv, TRUE);
    g_free(static_cache);
            g_free(ipc_path);
    g_free(wall_log);
            g_free(xwinwrap_path);
            g_free(source);
            g_free(video);
            g_free(stream_url);
            g_free(mode);
            g_key_file_unref(kf);
            g_free(cfg);
            return FALSE;
        }
        g_ptr_array_add(argv, g_strdup("--loop-file=inf"));
        play_media = g_strdup(static_cache);
    } else {
        if (loop && !is_stream)
            g_ptr_array_add(argv, g_strdup("--loop-file=inf"));
        play_media = g_strdup(media);
    }

    if (is_stream) {
        if (url_is_direct_stream(media)) {
            /* Let mpv/FFmpeg handle direct stream continuity itself. */
            if (!g_str_has_prefix(media, "rtsp://"))
                g_ptr_array_add(argv, g_strdup("--network-timeout=15"));
        } else {
            /* Webpage URLs such as YouTube use mpv's yt-dlp hook. */
            g_ptr_array_add(argv, g_strdup("--ytdl=yes"));
            g_ptr_array_add(argv, g_strdup("--script-opts=ytdl_hook-try_ytdl_first=yes"));
        }
    }
    if (hwdec)
        g_ptr_array_add(argv, g_strdup("--hwdec=auto-safe"));
    else
        g_ptr_array_add(argv, g_strdup("--hwdec=no"));

    GPtrArray *effects = backend_discover_effects();

    gboolean effect_added = FALSE;
    gchar *active_effect_id =
        g_key_file_has_key(kf, "effects", "active", NULL)
            ? g_key_file_get_string(kf, "effects", "active", NULL)
            : NULL;

    if (active_effect_id && *active_effect_id) {
        BackendEffect *active =
            backend_effect_by_id(effects, active_effect_id);

        if (active) {
            BackendParam *activation = backend_activation_param(active);
            gdouble value = activation
                ? backend_param_value(kf, active, activation)
                : 0.0;

            if (!activation || value > activation->min + 0.001) {
                add_backend_effect_shader(argv, kf, active);
                effect_added = TRUE;
            }
        }
    }

    /*
     * Backward compatibility with configs written before effects.active was
     * introduced. Once the UI saves again this path is no longer needed.
     */
    if (!effect_added && (!active_effect_id || !*active_effect_id)) {
        for (guint i = 0; effects && i < effects->len; i++) {
            BackendEffect *e = g_ptr_array_index(effects, i);
            BackendParam *p = backend_activation_param(e);
            if (!p)
                continue;

            gdouble value = backend_param_value(kf, e, p);
            if (value > p->min + 0.001) {
                add_backend_effect_shader(argv, kf, e);
                effect_added = TRUE;
                break;
            }
        }
    }

    /*
     * mpv's modern user shader path (and especially //!PARAM DYNAMIC used by
     * the audio-reactive system) needs gpu-next. The preview already uses
     * this renderer, so make the desktop wallpaper use the same renderer
     * whenever an effect shader is active.
     */
    g_free(active_effect_id);

    if (interpolation) {
        g_ptr_array_add(argv, g_strdup("--interpolation=yes"));
        g_ptr_array_add(argv, g_strdup("--video-sync=display-resample"));
    }

    gchar *brightness_arg = g_strdup_printf("--brightness=%.0f", CLAMP(brightness, -100.0, 100.0));
    gchar *contrast_arg = g_strdup_printf("--contrast=%.0f", CLAMP(contrast, -100.0, 100.0));
    gchar *saturation_arg = g_strdup_printf("--saturation=%.0f", CLAMP(saturation, -100.0, 100.0));
    g_ptr_array_add(argv, brightness_arg);
    g_ptr_array_add(argv, contrast_arg);
    g_ptr_array_add(argv, saturation_arg);

    gchar *speed_arg = g_strdup_printf("--speed=%.3f", CLAMP(speed, 0.05, 4.0));
    g_ptr_array_add(argv, speed_arg);

    if (fps > 0) {
        gchar *fps_arg = g_strdup_printf("--vf-add=fps=%d", CLAMP(fps, 1, 240));
        g_ptr_array_add(argv, fps_arg);
    }

    if (g_strcmp0(mode, "stretch") == 0) {
        g_ptr_array_add(argv, g_strdup("--keepaspect=no"));
    } else if (g_strcmp0(mode, "fit") == 0) {
        g_ptr_array_add(argv, g_strdup("--keepaspect=yes"));
        g_ptr_array_add(argv, g_strdup("--panscan=0.0"));
    } else {
        g_ptr_array_add(argv, g_strdup("--keepaspect=yes"));
        g_ptr_array_add(argv, g_strdup("--panscan=1.0"));
    }

    g_ptr_array_add(argv, play_media);
    g_ptr_array_add(argv, NULL);

    GPid child = 0;
    /*
     * Detach the long-running wallpaper process from the controller's stdio.
     * The GTK frontend invokes this controller synchronously and captures its
     * output. If xwinwrap/mpv inherit those pipes, they keep them open forever
     * and g_spawn_sync() in the frontend never sees EOF, making the UI appear
     * frozen after Set Wallpaper is pressed.
     */
    gboolean ok = g_spawn_async(NULL, (gchar **)argv->pdata, NULL,
                                G_SPAWN_SEARCH_PATH |
                                G_SPAWN_DO_NOT_REAP_CHILD |
                                G_SPAWN_STDOUT_TO_DEV_NULL |
                                G_SPAWN_STDERR_TO_DEV_NULL,
                                child_setup, NULL, &child, &err);
    if (!ok) {
        g_printerr("Failed to start wallpaper: %s\n", err->message);
        g_clear_error(&err);
    } else if (!write_pid(child, &err)) {
        g_printerr("Wallpaper started, but PID file could not be written: %s\n", err->message);
        g_clear_error(&err);
    }

    if (ok && desktop_icons)
        start_desktop_icons();

    if (ok && audio_enabled) {
        g_usleep(250000);
        start_audio_visualizer();
    }

    g_ptr_array_free(argv, TRUE);
    if (effects)
        g_ptr_array_free(effects, TRUE);
    g_free(ipc_path);
    g_free(wall_log);
    g_free(xwinwrap_path);
    g_free(source);
    g_free(video);
    g_free(stream_url);
    g_free(mode);
    g_key_file_unref(kf);
    g_free(cfg);
    return ok;
}

static void print_status(void) {
    GPid pid = read_pid();
    if (process_alive(pid)) {
        g_print("running (%d)\n", (int)pid);
    } else {
        remove_pidfile();
        g_print("stopped\n");
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        g_printerr("Usage: %s {start|stop|restart|status|autostart}\n", argv[0]);
        return 2;
    }
    if (g_strcmp0(argv[1], "start") == 0) return start_wallpaper(FALSE) ? 0 : 1;
    if (g_strcmp0(argv[1], "stop") == 0) return stop_wallpaper() ? 0 : 1;
    if (g_strcmp0(argv[1], "restart") == 0) {
        stop_wallpaper();
        return start_wallpaper(FALSE) ? 0 : 1;
    }
    if (g_strcmp0(argv[1], "status") == 0) {
        print_status();
        return 0;
    }
    if (g_strcmp0(argv[1], "autostart") == 0) {
        if (!wait_for_safe_desktop()) return 0;
        return start_wallpaper(TRUE) ? 0 : 1;
    }
    g_printerr("Unknown command: %s\n", argv[1]);
    return 2;
}

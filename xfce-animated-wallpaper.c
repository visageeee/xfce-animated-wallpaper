#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

static gchar *config_path(void) {
    return g_build_filename(g_get_user_config_dir(), "xfce-animated-wallpaper", "config.ini", NULL);
}

static gchar *pid_path(void) {
    return g_build_filename(g_get_user_runtime_dir() ? g_get_user_runtime_dir() : "/tmp",
                            "xfce-animated-wallpaper.pid", NULL);
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
    gdouble brightness = get_double(kf, "advanced", "brightness", 0.0);
    gdouble contrast = get_double(kf, "advanced", "contrast", 0.0);
    gdouble saturation = get_double(kf, "advanced", "saturation", 0.0);
    gdouble blur = get_double(kf, "advanced", "blur", 0.0);

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

    const gchar *xw_args[] = {
        "xwinwrap", "-ov", "-fs", "-ni", "-b", "-nf", "--",
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

    if (mute) g_ptr_array_add(argv, g_strdup("--no-audio"));
    if (loop && !is_stream) g_ptr_array_add(argv, g_strdup("--loop-file=inf"));

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
    /* Software libavfilter blur needs CPU frames. GIFs are normally decoded in
     * software already, while MP4/H.264 often uses hardware decoding. Force
     * software decoding only when blur is active so the filter behaves
     * consistently across formats. */
    if (blur > 0.01)
        g_ptr_array_add(argv, g_strdup("--hwdec=no"));
    else if (hwdec)
        g_ptr_array_add(argv, g_strdup("--hwdec=auto-safe"));
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
    if (blur > 0.01) {
        gchar *blur_arg = g_strdup_printf("--vf-add=lavfi=[gblur=sigma=%.2f]", CLAMP(blur, 0.0, 20.0));
        g_ptr_array_add(argv, blur_arg);
    }

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

    g_ptr_array_add(argv, g_strdup(media));
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

    g_ptr_array_free(argv, TRUE);
    g_free(wall_log);
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

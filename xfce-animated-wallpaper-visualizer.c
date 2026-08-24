#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SAMPLE_RATE 48000
#define HISTORY 1024

typedef struct {
    GtkWidget *window;
    GtkWidget *area;
    GPid parec_pid;
    GIOChannel *audio_channel;
    guint audio_watch;
    guint control_timer;
    gfloat samples[HISTORY];
    guint write_pos;
    guint sample_count;
    gdouble smoothed_peak;
    gdouble smoothed_bass;
    gdouble bass_lp_40;
    gdouble bass_lp_180;
    gdouble bass_floor;
    gdouble bass_ceiling;
    gchar *audio_source;
    gchar *audio_device;
    gchar *capture_device;
    guint device_rescan_timer;
    gdouble sensitivity;
    gdouble smoothing;
    gboolean control_mode;
    gboolean show_waveform;
    gchar *ipc_path;
} Visualizer;

static gchar *config_path(void) {
    return g_build_filename(g_get_user_config_dir(), "xfce-animated-wallpaper", "config.ini", NULL);
}

static gchar *audio_ipc_path(void) {
    const gchar *runtime = g_get_user_runtime_dir();
    return g_build_filename(runtime ? runtime : "/tmp", "xfce-animated-wallpaper-mpv.sock", NULL);
}

static void load_audio_config(Visualizer *v) {
    gchar *path = config_path();
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        v->show_waveform = g_key_file_has_key(kf, "audio", "show_waveform", NULL)
            ? g_key_file_get_boolean(kf, "audio", "show_waveform", NULL) : TRUE;
        v->sensitivity = g_key_file_has_key(kf, "audio", "sensitivity", NULL)
            ? g_key_file_get_double(kf, "audio", "sensitivity", NULL) : 2.0;
        v->smoothing = g_key_file_has_key(kf, "audio", "smoothing", NULL)
            ? g_key_file_get_double(kf, "audio", "smoothing", NULL) : 0.82;
        v->audio_source = g_key_file_has_key(kf, "audio", "source", NULL)
            ? g_key_file_get_string(kf, "audio", "source", NULL)
            : g_strdup("bass");
        v->audio_device = g_key_file_has_key(kf, "audio", "device", NULL)
            ? g_key_file_get_string(kf, "audio", "device", NULL)
            : g_strdup("automatic");
    } else {
        v->show_waveform = TRUE;
        v->sensitivity = 2.0;
        v->smoothing = 0.82;
        v->audio_source = g_strdup("bass");
        v->audio_device = g_strdup("automatic");
    }
    v->sensitivity = CLAMP(v->sensitivity, 0.1, 10.0);
    v->smoothing = CLAMP(v->smoothing, 0.0, 0.98);
    if (!v->audio_source ||
        (g_strcmp0(v->audio_source, "bass") != 0 &&
         g_strcmp0(v->audio_source, "overall") != 0)) {
        g_free(v->audio_source);
        v->audio_source = g_strdup("bass");
    }
    if (!v->audio_device || !*v->audio_device) {
        g_free(v->audio_device);
        v->audio_device = g_strdup("automatic");
    }
    v->bass_floor = 0.002;
    v->bass_ceiling = 0.06;
    g_key_file_unref(kf);
    g_free(path);
}

static void stop_capture_stream(Visualizer *v) {
    if (v->audio_watch) {
        g_source_remove(v->audio_watch);
        v->audio_watch = 0;
    }

    if (v->audio_channel) {
        g_io_channel_shutdown(v->audio_channel, TRUE, NULL);
        g_io_channel_unref(v->audio_channel);
        v->audio_channel = NULL;
    }

    if (v->parec_pid > 1) {
        kill(v->parec_pid, SIGTERM);
        g_spawn_close_pid(v->parec_pid);
        v->parec_pid = 0;
    }
}

static void stop_capture(Visualizer *v) {
    if (v->control_timer) {
        g_source_remove(v->control_timer);
        v->control_timer = 0;
    }
    if (v->device_rescan_timer) {
        g_source_remove(v->device_rescan_timer);
        v->device_rescan_timer = 0;
    }

    stop_capture_stream(v);

    g_free(v->capture_device);
    v->capture_device = NULL;
    g_free(v->ipc_path);
    v->ipc_path = NULL;
    g_free(v->audio_source);
    v->audio_source = NULL;
    g_free(v->audio_device);
    v->audio_device = NULL;
}

static GPtrArray *monitor_sources(void) {
    GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
    gchar *out = NULL;
    gint status = 0;
    GError *error = NULL;

    gchar *argv[] = {
        (gchar *)"pactl",
        (gchar *)"list",
        (gchar *)"short",
        (gchar *)"sources",
        NULL
    };

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &out, NULL, &status, &error)) {
        g_clear_error(&error);
        g_free(out);
        return sources;
    }

    if (!g_spawn_check_wait_status(status, NULL)) {
        g_free(out);
        return sources;
    }

    gchar **lines = g_strsplit(out ? out : "", "\n", -1);
    for (guint i = 0; lines && lines[i]; i++) {
        if (!*lines[i])
            continue;

        gchar **fields = g_strsplit(lines[i], "\t", 0);
        if (fields && fields[1] &&
            g_str_has_suffix(fields[1], ".monitor")) {
            g_ptr_array_add(sources, g_strdup(fields[1]));
        }
        g_strfreev(fields);
    }

    g_strfreev(lines);
    g_free(out);
    return sources;
}

static gdouble sample_monitor_rms(const gchar *source_name) {
    if (!source_name || !*source_name)
        return 0.0;

    gchar *quoted = g_shell_quote(source_name);

    /*
     * Capture about 180 ms from the monitor, convert the signed 16-bit PCM
     * to text with od, and calculate RMS in awk.  Producing text here avoids
     * the embedded-NUL problem of trying to measure raw PCM returned by
     * g_spawn_sync().
     */
    gchar *command = g_strdup_printf(
        "timeout 0.18s parec --device=%s --raw --format=s16le "
        "--rate=8000 --channels=1 2>/dev/null | "
        "od -An -v -t d2 | "
        "awk '{for(i=1;i<=NF;i++){x=$i/32768.0; s+=x*x; n++}} "
        "END{if(n>0) printf \"%%.9f\\n\", sqrt(s/n); else print \"0\"}'",
        quoted);

    gchar *out = NULL;
    gint status = 0;
    GError *error = NULL;

    gboolean ok = g_spawn_command_line_sync(
        command, &out, NULL, &status, &error);

    g_free(command);
    g_free(quoted);

    if (!ok) {
        g_clear_error(&error);
        g_free(out);
        return 0.0;
    }

    gdouble rms = out ? g_ascii_strtod(out, NULL) : 0.0;
    g_free(out);
    return MAX(rms, 0.0);
}

static gchar *automatic_monitor_source(void) {
    GPtrArray *sources = monitor_sources();
    if (!sources || sources->len == 0) {
        if (sources)
            g_ptr_array_free(sources, TRUE);
        return NULL;
    }

    gchar *best = NULL;
    gdouble best_score = -1.0;

    for (guint i = 0; i < sources->len; i++) {
        const gchar *name = g_ptr_array_index(sources, i);
        gdouble score = sample_monitor_rms(name);

        /*
         * Virtual ALSA loopback outputs are frequently default routing
         * devices but may contain silence. Prefer a real hardware monitor
         * when measured activity is otherwise indistinguishable.
         */
        if (score <= 0.000001 &&
            (strstr(name, "snd_aloop") || strstr(name, "loopback")))
            score = -0.5;

        if (!best || score > best_score) {
            g_free(best);
            best = g_strdup(name);
            best_score = score;
        }
    }

    g_ptr_array_free(sources, TRUE);
    return best;
}

static gchar *configured_monitor_source(Visualizer *v) {
    if (v->audio_device &&
        g_strcmp0(v->audio_device, "automatic") != 0)
        return g_strdup(v->audio_device);

    return automatic_monitor_source();
}




static gboolean on_audio_data(GIOChannel *source, GIOCondition condition, gpointer data) {
    Visualizer *v = data;
    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL))
        return G_SOURCE_REMOVE;

    guint8 raw[8192];
    gsize bytes_read = 0;
    GError *error = NULL;
    GIOStatus status = g_io_channel_read_chars(
        source, (gchar *)raw, sizeof(raw), &bytes_read, &error);

    if (status == G_IO_STATUS_ERROR) {
        g_clear_error(&error);
        return G_SOURCE_REMOVE;
    }
    if (bytes_read < 2)
        return G_SOURCE_CONTINUE;

    guint count = (guint)(bytes_read / 2);
    gint16 *pcm = (gint16 *)raw;
    gdouble local_peak = 0.0;
    gdouble bass_sq = 0.0;

    /* First-order low-pass filters at 40 and 180 Hz. Their difference is
     * a cheap 40–180 Hz bass band, sufficient for visual modulation. */
    const gdouble a40  = 1.0 - exp(-2.0 * G_PI * 40.0  / SAMPLE_RATE);
    const gdouble a180 = 1.0 - exp(-2.0 * G_PI * 180.0 / SAMPLE_RATE);

    for (guint i = 0; i < count; i++) {
        gdouble s = (gdouble)pcm[i] / 32768.0;

        v->bass_lp_40  += a40  * (s - v->bass_lp_40);
        v->bass_lp_180 += a180 * (s - v->bass_lp_180);
        gdouble bass = v->bass_lp_180 - v->bass_lp_40;
        bass_sq += bass * bass;

        local_peak = MAX(local_peak, fabs(s));

        if ((i & 3u) == 0) {
            v->samples[v->write_pos] = (gfloat)s;
            v->write_pos = (v->write_pos + 1) % HISTORY;
            if (v->sample_count < HISTORY)
                v->sample_count++;
        }
    }

    gdouble bass_rms = sqrt(bass_sq / MAX(count, 1u));

    /* Slowly adapting range: the ceiling follows strong bass quickly enough
     * to accommodate different tracks, while decaying slowly. The floor
     * follows quiet passages more slowly. */
    if (bass_rms > v->bass_ceiling)
        v->bass_ceiling = bass_rms;
    else
        v->bass_ceiling = v->bass_ceiling * 0.998 + bass_rms * 0.002;

    if (bass_rms < v->bass_floor)
        v->bass_floor = bass_rms;
    else
        v->bass_floor = v->bass_floor * 0.9995 + bass_rms * 0.0005;

    v->bass_ceiling = MAX(v->bass_ceiling, v->bass_floor + 0.004);

    gdouble normalized_bass =
        CLAMP((bass_rms - v->bass_floor) /
              (v->bass_ceiling - v->bass_floor), 0.0, 1.0);

    v->smoothed_peak =
        v->smoothed_peak * v->smoothing +
        local_peak * (1.0 - v->smoothing);

    v->smoothed_bass =
        v->smoothed_bass * v->smoothing +
        normalized_bass * (1.0 - v->smoothing);

    if (v->show_waveform && v->area)
        gtk_widget_queue_draw(v->area);

    return G_SOURCE_CONTINUE;
}

static gboolean start_capture(Visualizer *v) {
    gchar *monitor = configured_monitor_source(v);
    if (!monitor)
        return FALSE;

    g_free(v->capture_device);
    v->capture_device = g_strdup(monitor);

    g_print("Capturing audio from: %s\n", monitor);

    gchar *device_arg = g_strdup_printf("--device=%s", monitor);
    gchar *rate_arg = g_strdup_printf("--rate=%d", SAMPLE_RATE);
    gchar *argv[] = {(gchar *)"parec", device_arg, (gchar *)"--raw", (gchar *)"--format=s16le",
                     rate_arg, (gchar *)"--channels=1", NULL};
    gint stdout_fd = -1;
    GError *error = NULL;
    gboolean ok = g_spawn_async_with_pipes(NULL, argv, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, &v->parec_pid, NULL, &stdout_fd, NULL, &error);
    g_free(device_arg); g_free(rate_arg); g_free(monitor);
    if (!ok) { g_clear_error(&error); return FALSE; }

    v->audio_channel = g_io_channel_unix_new(stdout_fd);
    g_io_channel_set_encoding(v->audio_channel, NULL, NULL);
    g_io_channel_set_buffered(v->audio_channel, FALSE);
    GIOFlags flags = g_io_channel_get_flags(v->audio_channel);
    g_io_channel_set_flags(v->audio_channel, flags | G_IO_FLAG_NONBLOCK, NULL);
    v->audio_watch = g_io_add_watch(v->audio_channel,
        G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, on_audio_data, v);
    return TRUE;
}

static gboolean rescan_audio_device(gpointer data) {
    Visualizer *v = data;

    if (!v->audio_device ||
        g_strcmp0(v->audio_device, "automatic") != 0)
        return G_SOURCE_CONTINUE;

    gchar *best = automatic_monitor_source();
    if (!best)
        return G_SOURCE_CONTINUE;

    if (g_strcmp0(best, v->capture_device) != 0) {
        g_print("Audio monitor changed: %s -> %s\n",
                v->capture_device ? v->capture_device : "(none)",
                best);

        stop_capture_stream(v);

        g_free(v->capture_device);
        v->capture_device = NULL;

        /*
         * start_capture() performs a fresh automatic selection. This second
         * selection is intentional; it avoids keeping a source that vanished
         * between probe and restart.
         */
        start_capture(v);
    }

    g_free(best);
    return G_SOURCE_CONTINUE;
}


static gboolean write_all(int fd, const gchar *data, gsize len) {
    gsize offset = 0;

    while (offset < len) {
        ssize_t n = write(fd, data + offset, len - offset);

        if (n > 0) {
            offset += (gsize)n;
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        return FALSE;
    }

    return TRUE;
}

static gboolean send_audio_level(gpointer data) {
    Visualizer *v = data;
    if (!v->control_mode || !v->ipc_path) return G_SOURCE_CONTINUE;

    gdouble signal = g_strcmp0(v->audio_source, "overall") == 0
        ? v->smoothed_peak
        : v->smoothed_bass;
    gdouble level = CLAMP(signal * v->sensitivity, 0.0, 1.0);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return G_SOURCE_CONTINUE;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, v->ipc_path, sizeof addr.sun_path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
        gchar value[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(value, sizeof value, "%.5f", level);
        gchar *opts = g_strdup_printf("aw_audio=%s", value);
        gchar *json = g_strdup_printf(
            "{\"command\":[\"set_property\",\"glsl-shader-opts\",\"%s\"]}\n", opts);
        if (!write_all(fd, json, strlen(json))) {
            /* mpv may disappear between connect() and write(); simply retry
             * on the next visualizer tick. */
        }
        g_free(json); g_free(opts);
    }
    close(fd);
    return G_SOURCE_CONTINUE;
}

static gboolean draw_waveform(GtkWidget *widget, cairo_t *cr, gpointer data) {
    Visualizer *v = data;
    GtkAllocation a; gtk_widget_get_allocation(widget, &a);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0,0,0,0); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (v->sample_count < 2) return FALSE;

    const gdouble band_h = MIN(260.0, a.height * 0.28);
    const gdouble center_y = a.height - band_h * 0.56;
    const gdouble amplitude = band_h * 0.40;
    cairo_set_source_rgba(cr, 0,0,0,0.15);
    cairo_rectangle(cr, 0, a.height-band_h, a.width, band_h); cairo_fill(cr);

    for (int pass=0; pass<2; pass++) {
        cairo_set_source_rgba(cr, 1,1,1, pass==0 ? 0.16 : 0.82);
        cairo_set_line_width(cr, pass==0 ? 8.0 : 2.0);
        gboolean first=TRUE;
        for (guint x=0; x<(guint)a.width; x++) {
            gdouble frac=(a.width>1)?(gdouble)x/(a.width-1):0.0;
            guint logical=(guint)(frac*(v->sample_count-1));
            guint oldest=(v->write_pos+HISTORY-v->sample_count)%HISTORY;
            guint idx=(oldest+logical)%HISTORY;
            gdouble y=center_y-v->samples[idx]*amplitude;
            if(first){cairo_move_to(cr,x,y);first=FALSE;}else cairo_line_to(cr,x,y);
        }
        cairo_stroke(cr);
    }
    return FALSE;
}

static void make_click_through(GtkWidget *widget, gpointer data) {
    (void)data;
    GdkWindow *window=gtk_widget_get_window(widget); if(!window)return;
    cairo_region_t *empty=cairo_region_create();
    gdk_window_input_shape_combine_region(window,empty,0,0); cairo_region_destroy(empty);
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget; Visualizer *v=data; stop_capture(v); gtk_main_quit();
}

int main(int argc, char **argv) {
    gtk_init(&argc,&argv);
    Visualizer v={0};
    gchar *ipc_override = NULL;
    gboolean no_waveform = FALSE;

    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--control") == 0) {
            v.control_mode = TRUE;
        } else if (g_strcmp0(argv[i], "--no-waveform") == 0) {
            no_waveform = TRUE;
        } else if (g_strcmp0(argv[i], "--ipc-path") == 0 &&
                   i + 1 < argc) {
            ipc_override = g_strdup(argv[++i]);
        }
    }

    load_audio_config(&v);
    v.ipc_path = ipc_override ? ipc_override : audio_ipc_path();

    if (no_waveform)
        v.show_waveform = FALSE;
    else if (!v.control_mode)
        v.show_waveform = TRUE;

    if (v.show_waveform) {
        v.window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(v.window),FALSE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(v.window),TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(v.window),TRUE);
        gtk_window_set_accept_focus(GTK_WINDOW(v.window),FALSE);
        gtk_window_set_focus_on_map(GTK_WINDOW(v.window),FALSE);
        gtk_window_set_keep_below(GTK_WINDOW(v.window),TRUE);
        gtk_window_stick(GTK_WINDOW(v.window));
        gtk_widget_set_app_paintable(v.window,TRUE);
        GdkScreen *screen=gtk_widget_get_screen(v.window);
        GdkVisual *visual=gdk_screen_get_rgba_visual(screen); if(visual)gtk_widget_set_visual(v.window,visual);
        v.area=gtk_drawing_area_new(); gtk_container_add(GTK_CONTAINER(v.window),v.area);
        g_signal_connect(v.area,"draw",G_CALLBACK(draw_waveform),&v);
        g_signal_connect(v.window,"realize",G_CALLBACK(make_click_through),NULL);
        g_signal_connect(v.window,"destroy",G_CALLBACK(on_destroy),&v);
#if GTK_CHECK_VERSION(3,22,0)
        GdkDisplay *display=gdk_display_get_default();
        GdkMonitor *monitor=gdk_display_get_primary_monitor(display); if(!monitor)monitor=gdk_display_get_monitor(display,0);
        GdkRectangle geometry={0,0,1280,720}; if(monitor)gdk_monitor_get_geometry(monitor,&geometry);
        gtk_window_move(GTK_WINDOW(v.window),geometry.x,geometry.y);
        gtk_window_set_default_size(GTK_WINDOW(v.window),geometry.width,geometry.height);
#endif
        gtk_widget_show_all(v.window);
        gtk_window_present(GTK_WINDOW(v.window));
        gtk_window_set_keep_below(GTK_WINDOW(v.window),TRUE);
    }

    if(!start_capture(&v)) return 1;
    if(v.control_mode)
        v.control_timer=g_timeout_add(33,send_audio_level,&v);
    if(v.audio_device &&
       g_strcmp0(v.audio_device,"automatic")==0)
        v.device_rescan_timer =
            g_timeout_add_seconds(10,rescan_audio_device,&v);
    gtk_main();
    return 0;
}

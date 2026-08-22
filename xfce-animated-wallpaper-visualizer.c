#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib.h>
#include <math.h>
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
    } else {
        v->show_waveform = TRUE;
        v->sensitivity = 2.0;
        v->smoothing = 0.82;
        v->audio_source = g_strdup("bass");
    }
    v->sensitivity = CLAMP(v->sensitivity, 0.1, 10.0);
    v->smoothing = CLAMP(v->smoothing, 0.0, 0.98);
    if (!v->audio_source ||
        (g_strcmp0(v->audio_source, "bass") != 0 &&
         g_strcmp0(v->audio_source, "overall") != 0)) {
        g_free(v->audio_source);
        v->audio_source = g_strdup("bass");
    }
    v->bass_floor = 0.002;
    v->bass_ceiling = 0.06;
    g_key_file_unref(kf);
    g_free(path);
}

static void stop_capture(Visualizer *v) {
    if (v->control_timer) g_source_remove(v->control_timer);
    if (v->audio_watch) g_source_remove(v->audio_watch);
    if (v->audio_channel) {
        g_io_channel_shutdown(v->audio_channel, TRUE, NULL);
        g_io_channel_unref(v->audio_channel);
    }
    if (v->parec_pid > 1) {
        kill(v->parec_pid, SIGTERM);
        g_spawn_close_pid(v->parec_pid);
    }
    g_free(v->ipc_path);
    g_free(v->audio_source);
}

static gchar *default_monitor_source(void) {
    gchar *out = NULL, *err = NULL;
    gint status = 0;
    GError *error = NULL;
    gchar *argv[] = {(gchar *)"pactl", (gchar *)"get-default-sink", NULL};
    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &out, &err, &status, &error)) {
        g_printerr("Could not run pactl: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error); g_free(out); g_free(err); return NULL;
    }
    if (!g_spawn_check_wait_status(status, NULL)) {
        g_free(out); g_free(err); return NULL;
    }
    g_strstrip(out);
    gchar *monitor = (out && *out) ? g_strdup_printf("%s.monitor", out) : NULL;
    g_free(out); g_free(err); return monitor;
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
    gchar *monitor = default_monitor_source();
    if (!monitor) return FALSE;
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
        write(fd, json, strlen(json));
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
    v.control_mode = argc > 1 && g_strcmp0(argv[1], "--control") == 0;
    load_audio_config(&v);
    v.ipc_path = audio_ipc_path();

    if (!v.control_mode) v.show_waveform = TRUE;

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
    if(v.control_mode) v.control_timer=g_timeout_add(33,send_audio_level,&v);
    gtk_main();
    return 0;
}

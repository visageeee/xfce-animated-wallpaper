#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <pango/pango.h>
#include <glib/gstdio.h>

typedef struct {
    gchar *path;
    gchar *display_name;
    GIcon *icon;

    gboolean has_saved_position;
    gint saved_row;
    gint saved_col;
} DesktopItem;

typedef struct {
    gchar *desktop_dir;
    GFileMonitor *monitor;
    GPtrArray *windows;
    guint rebuild_source;
    gint icon_size;
} IconApp;




typedef struct {
    gchar *path;
} ClickData;

static void desktop_item_free(gpointer data) {
    DesktopItem *item = data;
    if (!item) return;
    g_free(item->path);
    g_free(item->display_name);
    g_clear_object(&item->icon);
    g_free(item);
}

static gint desktop_item_sort(gconstpointer a, gconstpointer b) {
    const DesktopItem *ia = *(DesktopItem * const *)a;
    const DesktopItem *ib = *(DesktopItem * const *)b;
    return g_utf8_collate(ia->display_name, ib->display_name);
}

static void click_data_free(gpointer data, GClosure *closure) {
    (void)closure;
    ClickData *click = data;
    if (!click) return;
    g_free(click->path);
    g_free(click);
}

static gboolean launch_path(const gchar *path, GError **error) {
    gchar *lower = g_ascii_strdown(path, -1);
    gboolean is_desktop = g_str_has_suffix(lower, ".desktop");
    g_free(lower);

    if (is_desktop) {
        GDesktopAppInfo *desktop_app = g_desktop_app_info_new_from_filename(path);
        if (desktop_app) {
            gboolean ok = g_app_info_launch(G_APP_INFO(desktop_app), NULL, NULL, error);
            g_object_unref(desktop_app);
            if (ok) return TRUE;
        }
    }

    gchar *uri = g_filename_to_uri(path, NULL, error);
    if (!uri) return FALSE;
    gboolean ok = g_app_info_launch_default_for_uri(uri, NULL, error);
    g_free(uri);
    return ok;
}

static gboolean on_icon_button(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    ClickData *click = data;
    if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
        GError *error = NULL;
        if (!launch_path(click->path, &error)) {
            g_warning("Could not open %s: %s", click->path,
                      error ? error->message : "unknown error");
            g_clear_error(&error);
        }
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *make_icon_window(DesktopItem *item, gint icon_size) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_below(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_NORMAL);
    gtk_widget_set_app_paintable(window, TRUE);
    gtk_widget_set_size_request(window, 112, 94);

    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(window, visual);
    gtk_style_context_add_class(gtk_widget_get_style_context(window), "desktop-icon-window");

    GtkWidget *event = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event), FALSE);
    gtk_widget_add_events(event, GDK_BUTTON_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(window), event);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    gtk_container_add(GTK_CONTAINER(event), box);

    GtkWidget *image = gtk_image_new_from_gicon(item->icon, GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(image), icon_size);
    gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

    GtkWidget *label = gtk_label_new(item->display_name);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 14);
    gtk_label_set_lines(GTK_LABEL(label), 2);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(label, 104, -1);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "desktop-icon-label");
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    gtk_widget_set_tooltip_text(event, item->path);

    ClickData *click = g_new0(ClickData, 1);
    click->path = g_strdup(item->path);
    g_signal_connect_data(event, "button-press-event", G_CALLBACK(on_icon_button),
                          click, click_data_free, 0);
    return window;
}

static void clear_windows(IconApp *app) {
    if (!app->windows) return;
    for (guint i = 0; i < app->windows->len; i++) {
        GtkWidget *window = g_ptr_array_index(app->windows, i);
        if (GTK_IS_WIDGET(window)) gtk_widget_destroy(window);
    }
    g_ptr_array_set_size(app->windows, 0);
}


static gint layout_match_count(const gchar *path, GPtrArray *items) {
    GKeyFile *kf = g_key_file_new();
    gint matches = 0;

    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        for (guint i = 0; items && i < items->len; i++) {
            DesktopItem *item = g_ptr_array_index(items, i);
            if (g_key_file_has_group(kf, item->path))
                matches++;
        }
    }

    g_key_file_unref(kf);
    return matches;
}

static gchar *xfdesktop_layout_path_for_workarea(const GdkRectangle *workarea,
                                                 GPtrArray *items) {
    gchar *desktop_cfg = g_build_filename(g_get_user_config_dir(),
                                          "xfce4", "desktop", NULL);

    GDir *dir = g_dir_open(desktop_cfg, 0, NULL);
    if (!dir) {
        g_free(desktop_cfg);
        return NULL;
    }

    gchar *best = NULL;
    gint best_matches = -1;
    gint best_score = G_MAXINT;
    const gchar *entry;

    while ((entry = g_dir_read_name(dir))) {
        gint w = 0, h = 0;
        if (sscanf(entry, "icons.screen0-%dx%d.rc", &w, &h) != 2)
            continue;

        gchar *candidate = g_build_filename(desktop_cfg, entry, NULL);
        gint matches = layout_match_count(candidate, items);

        gint dw = ABS(w - workarea->width);
        gint dh = ABS(h - workarea->height);
        gint score = dw + dh * 4;

        /*
         * Prefer the file that knows about the most of our current Desktop
         * items. Screen geometry is only a tiebreaker.
         */
        if (matches > best_matches ||
            (matches == best_matches && score < best_score)) {
            g_free(best);
            best = candidate;
            best_matches = matches;
            best_score = score;
        } else {
            g_free(candidate);
        }
    }

    g_dir_close(dir);

    g_free(desktop_cfg);
    return best;
}

static void apply_xfdesktop_positions(GPtrArray *items,
                                      const GdkRectangle *workarea) {
    gchar *layout_path = xfdesktop_layout_path_for_workarea(workarea, items);
    if (!layout_path)
        return;

    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, layout_path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(layout_path);
        return;
    }

    for (guint i = 0; i < items->len; i++) {
        DesktopItem *item = g_ptr_array_index(items, i);

        if (!g_key_file_has_group(kf, item->path))
            continue;

        GError *error = NULL;
        gint row = g_key_file_get_integer(kf, item->path, "row", &error);
        if (error) {
            g_clear_error(&error);
            continue;
        }

        gint col = g_key_file_get_integer(kf, item->path, "col", &error);
        if (error) {
            g_clear_error(&error);
            continue;
        }

        if (row >= 0 && col >= 0) {
            item->saved_row = row;
            item->saved_col = col;
            item->has_saved_position = TRUE;
        }
    }

    g_key_file_unref(kf);
    g_free(layout_path);
}

static GPtrArray *read_desktop_items(const gchar *desktop_dir) {
    GPtrArray *items = g_ptr_array_new_with_free_func(desktop_item_free);
    GFile *dir = g_file_new_for_path(desktop_dir);
    GError *error = NULL;
    GFileEnumerator *enumerator = g_file_enumerate_children(
        dir,
        G_FILE_ATTRIBUTE_STANDARD_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_ICON ","
        G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
        G_FILE_QUERY_INFO_NONE, NULL, &error);

    if (!enumerator) {
        g_warning("Could not read desktop directory %s: %s", desktop_dir,
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(dir);
        return items;
    }

    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(enumerator, NULL, &error))) {
        if (g_file_info_get_is_hidden(info)) {
            g_object_unref(info);
            continue;
        }
        const gchar *name = g_file_info_get_name(info);
        const gchar *display = g_file_info_get_display_name(info);
        GIcon *icon = g_file_info_get_icon(info);

        if (!name || !*name) {
            g_object_unref(info);
            continue;
        }

        DesktopItem *item = g_new0(DesktopItem, 1);
        item->path = g_build_filename(desktop_dir, name, NULL);

        /*
         * .desktop launchers carry their real display name and assigned icon
         * inside the desktop file. The generic GFileInfo icon only describes
         * the file itself, which is why launchers previously appeared with a
         * generic application/document icon.
         */
        gchar *lower_name = g_ascii_strdown(name, -1);
        gboolean is_desktop =
            g_str_has_suffix(lower_name, ".desktop");
        g_free(lower_name);

        if (is_desktop) {
            GDesktopAppInfo *app_info =
                g_desktop_app_info_new_from_filename(item->path);

            if (app_info) {
                const gchar *app_name =
                    g_app_info_get_display_name(G_APP_INFO(app_info));
                GIcon *app_icon =
                    g_app_info_get_icon(G_APP_INFO(app_info));

                item->display_name =
                    g_strdup(app_name && *app_name ? app_name : name);

                if (app_icon)
                    item->icon = g_object_ref(app_icon);

                g_object_unref(app_info);
            }

            /*
             * Some hand-written or untrusted desktop files may not load as a
             * GDesktopAppInfo. Still hide the .desktop suffix in that case.
             */
            if (!item->display_name) {
                gsize len = strlen(name);
                gsize suffix_len = strlen(".desktop");
                item->display_name =
                    len > suffix_len
                        ? g_strndup(name, len - suffix_len)
                        : g_strdup(name);
            }

            if (!item->icon)
                item->icon =
                    icon ? g_object_ref(icon)
                         : g_themed_icon_new("application-x-executable");
        } else {
            item->display_name =
                g_strdup(display && *display ? display : name);
            item->icon =
                icon ? g_object_ref(icon)
                     : g_themed_icon_new("text-x-generic");
        }

        g_ptr_array_add(items, item);
        g_object_unref(info);
    }

    if (error) {
        g_warning("Error while reading desktop: %s", error->message);
        g_clear_error(&error);
    }
    g_object_unref(enumerator);
    g_object_unref(dir);
    g_ptr_array_sort(items, desktop_item_sort);
    return items;
}




static gboolean rebuild_icons(gpointer data) {
    IconApp *app = data;
    app->rebuild_source = 0;
    clear_windows(app);
    GPtrArray *items = read_desktop_items(app->desktop_dir);

    GdkDisplay *display = gdk_display_get_default();
    GdkRectangle workarea = {0, 0, 1280, 720};
#if GTK_CHECK_VERSION(3,22,0)
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) monitor = gdk_display_get_monitor(display, 0);
    if (monitor) gdk_monitor_get_workarea(monitor, &workarea);
#else
    GdkScreen *screen = gdk_screen_get_default();
    workarea.width = gdk_screen_get_width(screen);
    workarea.height = gdk_screen_get_height(screen);
#endif
    apply_xfdesktop_positions(items, &workarea);

    const gint margin_x = 12, margin_y = 18, cell_w = 116, cell_h = 98;
    gint rows = MAX(1, (workarea.height - margin_y * 2) / cell_h);

    for (guint i = 0; i < items->len; i++) {
        DesktopItem *item = g_ptr_array_index(items, i);
        GtkWidget *window = make_icon_window(item, app->icon_size);
        gint row;
        gint col;

        if (item->has_saved_position) {
            row = item->saved_row;
            col = item->saved_col;
        } else {
            col = (gint)i / rows;
            row = (gint)i % rows;
        }

        gint requested_x = workarea.x + margin_x + col * cell_w;
        gint requested_y = workarea.y + margin_y + row * cell_h;

        gtk_window_move(GTK_WINDOW(window), requested_x, requested_y);
        gtk_widget_show_all(window);
        gtk_window_present(GTK_WINDOW(window));
        gtk_window_set_keep_below(GTK_WINDOW(window), TRUE);
        g_ptr_array_add(app->windows, window);
    }

    g_ptr_array_free(items, TRUE);
    return G_SOURCE_REMOVE;
}

static void schedule_rebuild(IconApp *app, guint delay_ms) {
    if (app->rebuild_source) g_source_remove(app->rebuild_source);
    app->rebuild_source = g_timeout_add(delay_ms, rebuild_icons, app);
}

static void on_desktop_changed(GFileMonitor *monitor, GFile *file,
                               GFile *other_file, GFileMonitorEvent event_type,
                               gpointer data) {
    (void)monitor; (void)file; (void)other_file; (void)event_type;
    schedule_rebuild((IconApp *)data, 200);
}

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css =
        ".desktop-icon-window { background-color: transparent; }"
        ".desktop-icon-label { color: white; font-weight: 500;"
        " text-shadow: 0 1px 3px rgba(0,0,0,0.95);"
        " background-color: rgba(0,0,0,0.18); border-radius: 4px; padding: 1px 3px; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    IconApp app = {0};
    app.icon_size = 48;
    app.windows = g_ptr_array_new();

    const gchar *desktop = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    if (!desktop || !*desktop) desktop = g_get_home_dir();
    app.desktop_dir = g_strdup(desktop);

    if (!g_file_test(app.desktop_dir, G_FILE_TEST_IS_DIR)) {
        g_printerr("Desktop directory does not exist: %s\n", app.desktop_dir);
        g_free(app.desktop_dir);
        g_ptr_array_free(app.windows, TRUE);
        return 1;
    }

    apply_css();
    GFile *dir = g_file_new_for_path(app.desktop_dir);
    GError *error = NULL;
    app.monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE, NULL, &error);
    g_object_unref(dir);
    if (app.monitor)
        g_signal_connect(app.monitor, "changed", G_CALLBACK(on_desktop_changed), &app);
    else {
        g_warning("Could not monitor desktop directory: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
    }

    schedule_rebuild(&app, 700);
    gtk_main();

    if (app.rebuild_source) g_source_remove(app.rebuild_source);
    clear_windows(&app);
    g_clear_object(&app.monitor);
    g_ptr_array_free(app.windows, TRUE);
    g_free(app.desktop_dir);
    return 0;
}

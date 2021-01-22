/*
 * splashscreen-util.c
 *
 * Copyright (C) 2021 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <canberra.h>
#include <systemui.h>

#include <getopt.h>
#include <unistd.h>
#include <syslog.h>

#include "splashscreen-dbus-names.h"

static GtkWidget *window = NULL;
static GtkWidget *image = NULL;
static gchar *bootup_image_filename = NULL;
static gchar *shutdown_image_filename = NULL;
static gchar *shutdown_sound_filename = NULL;
static ca_context *c = NULL;
static GdkGeometry geometry = {};

#define CRITICAL(msg, ...) \
  syslog(LOG_MAKEPRI(LOG_USER, LOG_CRIT), msg "\n", ##__VA_ARGS__)

#define WARN(msg, ...) \
  syslog(LOG_MAKEPRI(LOG_USER, LOG_WARNING), msg "\n", ##__VA_ARGS__)

#define INFO(msg, ...) \
  syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), msg "\n", ##__VA_ARGS__)

static GdkPixbuf *
_pixbuf_rotate_if_needed(GdkPixbuf *pixbuf)
{
  GdkPixbuf *rotated;

  if (!GDK_IS_PIXBUF(pixbuf))
  {
    WARN("not a valid pixbuf for rotation");
    return pixbuf;
  }

  if (gdk_screen_width() >= gdk_screen_height())
    return pixbuf;

  rotated = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
  g_object_unref(pixbuf);

  return rotated;
}

static GdkPixbuf *
_load_image(const char *filename)
{
  GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
  GError *error = NULL;
  GdkPixbuf *pixbuf = NULL;

  g_assert(filename != NULL);

  if (g_strrstr(filename, "/"))
  {
    pixbuf = gdk_pixbuf_new_from_file(filename, &error);

    if (error)
    {
      CRITICAL("gdk_pixbuf_new_from_file failed %s", error->message);
      g_error_free(error);
    }
  }
  else
  {
    GtkIconInfo *info = gtk_icon_theme_lookup_icon(icon_theme, filename, 1,
                                                   GTK_ICON_LOOKUP_NO_SVG);

    if (info)
    {
      gint size = gtk_icon_info_get_base_size(info);

      if (size <= 0)
        size = 38;

      pixbuf = gtk_icon_theme_load_icon(icon_theme, filename, size,
                                        GTK_ICON_LOOKUP_NO_SVG, &error);
      gtk_icon_info_free(info);

      if (error)
      {
        CRITICAL("failed to load icon %s", error->message);
        g_error_free(error);
      }
    }
    else
      CRITICAL("failed to get icon info");
  }

  return pixbuf;
}

static void
_cleanup()
{
  if (image)
  {
    gtk_widget_destroy(image);
    image = NULL;
  }

  if (window)
  {
    gtk_widget_destroy(window);
    window = NULL;
  }
}

static void
_set_image(GtkWidget **image, const char *filename)
{
  GdkPixbuf *pixbuf = _load_image(filename);
  int h;
  int w;
  int width;
  int height;
  GdkPixbuf *scaled;

  g_assert(pixbuf != NULL);

  pixbuf = _pixbuf_rotate_if_needed(pixbuf);

  width = gdk_pixbuf_get_width(pixbuf);
  height = gdk_pixbuf_get_height(pixbuf);

  w = geometry.max_width;
  h = geometry.max_height;

  if (width <= w && height <= h)
    scaled = gdk_pixbuf_copy(pixbuf);
  else
  {
    if (w * height > width * h)
      w = width * h / height;
    else
      h = height * w / width;

    scaled = gdk_pixbuf_scale_simple(pixbuf, w, h, GDK_INTERP_BILINEAR);
  }

  if (!*image)
  {
    *image = gtk_image_new_from_pixbuf(scaled);

    if (*image)
    {
      gtk_widget_show(*image);
      gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(*image));
    }
    else
      CRITICAL("gtk_image_new_from_pixbuf failed");
  }
  else
    gtk_image_set_from_pixbuf(GTK_IMAGE(*image), scaled);

  if (scaled)
    g_object_unref(scaled);

  if (pixbuf)
    g_object_unref(pixbuf);
}

int
main(int argc, char **argv)
{
  GConfClient *gc_client;
  int option_index = 0;
  static int sound = TRUE;
  static int no_window = TRUE;
  static int type = SPLASHSCREEN_ENABLE_SHUTDOWN;
  static const struct option longopts[] =
  {
    {"sound", no_argument, &sound, TRUE},
    {"no-sound", no_argument, &sound, FALSE},
    {"shutdown", no_argument, &type, SPLASHSCREEN_ENABLE_SHUTDOWN},
    {"bootup", no_argument, &type, SPLASHSCREEN_ENABLE_BOOTUP},
    {"window", no_argument, &no_window, FALSE},
    {"no-window", no_argument, &no_window, TRUE},
    {0, 0, 0, 0}
  };

  openlog("splashscreen-util", LOG_PID | LOG_NDELAY , LOG_USER);

  while (getopt_long(argc, argv, "", longopts, &option_index) != -1)
    ;

  INFO("type '%d' (%s), sound '%d' (%s), logo type '%d' (%s)",
       type, type != SPLASHSCREEN_ENABLE_SHUTDOWN ? "BOOTUP" : "SHUTDOWN",
       sound, sound ? "YES" : "NO",
       no_window, no_window ? "BACKGROUND" : "WINDOW");

  gtk_init(&argc, &argv);
  gc_client = gconf_client_get_default();

  g_return_val_if_fail(gc_client != NULL, TRUE);

  bootup_image_filename = gconf_client_get_string(
        gc_client, "/system/systemui/splash/bootup_image", NULL);
  shutdown_image_filename = gconf_client_get_string(
        gc_client, "/system/systemui/splash/shutdown_image", NULL);
  shutdown_sound_filename = gconf_client_get_string(
        gc_client, "/system/systemui/splash/shutdown_soundfilename", NULL);

  /* WTF?!? */
  if (!bootup_image_filename)
    bootup_image_filename = g_strdup("/tmp/foo.gif");

  if (!shutdown_image_filename)
    shutdown_image_filename = g_strdup("/tmp/bar.gif");

  if (!shutdown_sound_filename)
    shutdown_sound_filename = g_strdup("/usr/share/sounds/ui-shutdown.wav");

  if (no_window)
  {
    GdkPixmap *drawable = NULL;
    Display *dpy = GDK_DISPLAY();
    Window root_window = GDK_ROOT_WINDOW();
    GdkPixbuf *pixbuf;

    INFO("setting root window background to Nokia logo");

    pixbuf = _pixbuf_rotate_if_needed(_load_image(shutdown_image_filename));
    if (pixbuf)
    {
      gdk_pixbuf_render_pixmap_and_mask(pixbuf, &drawable, NULL, 255);
      g_object_unref(pixbuf);
    }

    if (drawable)
    {
      gdk_error_trap_push();
      XSetWindowBackgroundPixmap(dpy, root_window,
                                 gdk_x11_drawable_get_xid(drawable));
      XClearWindow(dpy, root_window);
      XFlush(dpy);

      if (gdk_error_trap_pop())
        CRITICAL("failed to set root window logo background");

      g_object_unref(drawable);
      INFO("root window bg set to show Nokia logo");
    }
    else
      CRITICAL("failed to create logo pixmap");
  }
  else
  {
    if (!window)
    {
      GdkColor color = {0, 0xFFFF, 0xFFFF, 0xFFFF};

      window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(window), "splash");
      gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
      gtk_window_fullscreen(GTK_WINDOW(window));
      gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DND);
      gtk_widget_add_events(GTK_WIDGET(window), GDK_EXPOSURE_MASK);
      gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
      gtk_widget_realize(window);
      gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &color);

      geometry.min_width = gdk_screen_get_width(
            gdk_drawable_get_screen(GDK_DRAWABLE(GTK_WIDGET(window)->window)));
      geometry.max_width = geometry.min_width;

      geometry.min_height = gdk_screen_get_height(
            gdk_drawable_get_screen(GDK_DRAWABLE(GTK_WIDGET(window)->window)));
      geometry.max_height = geometry.min_height;

      gtk_window_set_geometry_hints(GTK_WINDOW(window), window, &geometry,
                                    GDK_HINT_MAX_SIZE | GDK_HINT_MIN_SIZE);

      if (type == SPLASHSCREEN_ENABLE_BOOTUP)
        _set_image(&image, bootup_image_filename);
      else if (type == SPLASHSCREEN_ENABLE_SHUTDOWN)
        _set_image(&image, shutdown_image_filename);
      else
      {
        CRITICAL("SPLASH: NOT BOOTUP; NOT SHUTDOWN; ERROR!");
        _cleanup();
      }

      if (type == SPLASHSCREEN_ENABLE_BOOTUP ||
          type == SPLASHSCREEN_ENABLE_SHUTDOWN)
      {
        gtk_widget_show_all(window);

        while (gtk_events_pending())
          gtk_main_iteration();
      }
    }
    else
      CRITICAL("already visible, ignore");
  }

  if (sound)
  {
    int res = ca_context_create(&c);

    if (res)
      CRITICAL("ca_context_create: %s", ca_strerror(res));

    if (c)
    {
      res = ca_context_play(c, 0,
                            "media.filename", shutdown_sound_filename,
                            "media.name", "Shutdown notification",
                            NULL);
      INFO("ca_context_play: %s", ca_strerror(res));
    }
  }

  gtk_main();
  CRITICAL("this line should never be reached");
  _cleanup();

  if (c)
  {
    ca_context_destroy(c);
    c = NULL;
  }

  g_object_unref(gc_client);
  g_free(bootup_image_filename);
  g_free(shutdown_image_filename);
  g_free(shutdown_sound_filename);
  closelog();

  return 0;
}

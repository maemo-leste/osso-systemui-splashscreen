/*
 * osso-systemui-splashscreen.c
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

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dsme/dsme_dbus_if.h>
#include <systemui.h>

#include "splashscreen-dbus-names.h"

static system_ui_data *sui = NULL;
static DBusConnection *conn = NULL;
static gboolean app_mgr_running = FALSE;

#define HD_APP_MGR "com.nokia.HildonDesktop.AppMgr"
#define HD_APP_MGR_DBUS_MATCH \
  "type='signal',interface='" DBUS_INTERFACE_DBUS "',path='" DBUS_PATH_DBUS \
  "',member='NameOwnerChanged',arg0='" HD_APP_MGR "'"

static gchar *
_dsme_shutdown_ind_sig()
{
  return g_strdup_printf(
        "type='signal',interface='%s',path='%s',member='%s'",
        dsme_sig_interface, dsme_sig_path, dsme_shutdown_ind);
}

static void
splash(splashscreen_t mode, gboolean enable_sound, gboolean disable_window)
{
  const char *arg1;
  const char *arg2;
  const char *arg3;
  GError *error = NULL;
  gchar *cmd;

  if (g_file_test("/tmp/splashscreen-already-running", G_FILE_TEST_EXISTS))
  {
    SYSTEMUI_INFO("already running from init.d, cancelling spawn");
    return;
  }

  if (mode == SPLASHSCREEN_ENABLE_BOOTUP)
    arg1 = "--bootup";
  else
    arg1 = "--shutdown";

  if (enable_sound)
    arg2 = "--sound";
  else
    arg2 = "--no-sound";

  if (disable_window)
    arg3 = "--no-window";
  else
    arg3 = "--window";

  cmd = g_strjoin(" ", "/usr/bin/splashscreen-util", arg1, arg2, arg3, NULL);

  if (!g_spawn_command_line_async(cmd, &error))
  {
    SYSTEMUI_CRITICAL("failed splash-util async spawn '%s': %s", cmd,
                      error->message);
    g_error_free(error);
  }

  g_free(cmd);
}

static DBusHandlerResult
_splashscreen_dbus_filter_cb(DBusConnection *connection, DBusMessage *msg,
                             void *user_data)
{
  const char *iface = dbus_message_get_interface(msg);
  const char *member = dbus_message_get_member(msg);
  const char *sender = dbus_message_get_sender(msg);
  int type = dbus_message_get_type(msg);

  if (!type || !iface || !sender || !member)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (type == DBUS_MESSAGE_TYPE_SIGNAL)
  {
    if (!g_ascii_strcasecmp(iface, dsme_sig_interface) &&
        !g_ascii_strcasecmp(member, dsme_shutdown_ind))
    {
      SYSTEMUI_INFO("shutdown_ind from DSME, running splashscreen-util");
      splash(SPLASHSCREEN_ENABLE_SHUTDOWN, TRUE, app_mgr_running);
    }
    else if (!g_ascii_strcasecmp(iface, DBUS_INTERFACE_DBUS) &&
             !g_ascii_strcasecmp(member, "NameOwnerChanged"))
    {
      const gchar *name = NULL;

      dbus_message_get_args(msg, NULL,
                            DBUS_TYPE_STRING, &name,
                            DBUS_TYPE_INVALID);

      if (!g_ascii_strcasecmp(name, HD_APP_MGR))
      {
        app_mgr_running = TRUE;
        dbus_bus_remove_match(conn, HD_APP_MGR_DBUS_MATCH, &sui->dbuserror);
        dbus_connection_remove_filter(conn, _splashscreen_dbus_filter_cb, NULL);
        dbus_connection_unref(conn);
        conn = NULL;
      }
    }
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
_setup_dbus()
{
  gchar *dsme_sig;

  if (!dbus_connection_add_filter(sui->system_bus,
                                  _splashscreen_dbus_filter_cb, sui, NULL))
  {
    SYSTEMUI_WARNING("Failed to add dbus filter");
    return FALSE;
  }

  dsme_sig = _dsme_shutdown_ind_sig();
  dbus_bus_add_match(sui->system_bus, dsme_sig, &sui->dbuserror);
  g_free(dsme_sig);

  if (dbus_error_is_set(&sui->dbuserror))
  {
    SYSTEMUI_WARNING("Unable to add match for shutdown ind signal %s",
                     sui->dbuserror.message);
    dbus_error_free(&sui->dbuserror);
    goto err_dsme;
  }

  conn = dbus_bus_get(DBUS_BUS_SESSION, &sui->dbuserror);

  if (!conn)
  {
    SYSTEMUI_WARNING("Failed to open connection to session bus");
    goto err_dsme;
  }

  dbus_connection_setup_with_g_main(conn, NULL);

  if (!dbus_connection_add_filter(conn, _splashscreen_dbus_filter_cb, NULL,
                                  NULL))
  {
    SYSTEMUI_WARNING("Failed to add dbus session filter");
    goto err_conn;
  }

  dbus_bus_add_match(conn, HD_APP_MGR_DBUS_MATCH, &sui->dbuserror);

  if (dbus_error_is_set(&sui->dbuserror))
  {
    SYSTEMUI_WARNING("Unable to add match for desktop owner changed signal %s",
                     sui->dbuserror.message);
    goto err_session;
  }

  return TRUE;

err_session:
  dbus_connection_remove_filter(conn, _splashscreen_dbus_filter_cb, NULL);
err_conn:
  dbus_connection_unref(conn);
  conn = NULL;
err_dsme:
  dbus_connection_remove_filter(sui->system_bus, _splashscreen_dbus_filter_cb,
                                sui);

  return FALSE;

}

int
splashscreen_open_handler(const char *interface, const char *method,
                          GArray *args, system_ui_data *data,
                          system_ui_handler_arg *out)
{
  int supported_args[] = {'u', 'b'};
  gint argc = args->len - 4;
  dbus_bool_t enable_sound = FALSE;
  system_ui_handler_arg* hargs;

  if (argc < 0 || !systemui_check_plugin_arguments(args, supported_args, argc))
  {
    SYSTEMUI_ERROR("Called with wrong number of arguments %d", args->len);
    return 0;
  }

  hargs = ((system_ui_handler_arg *)args->data);

  if (argc == 2)
    enable_sound = hargs[5].data.bool_val;

  if (hargs[4].data.u32 == SPLASHSCREEN_ENABLE_BOOTUP)
    splash(SPLASHSCREEN_ENABLE_BOOTUP, enable_sound, FALSE);

  return DBUS_TYPE_VARIANT;
}

static int
splashscreen_close_handler(const char *interface, const char *method,
                           GArray *args, system_ui_data *data,
                           system_ui_handler_arg *out)
{
  return DBUS_TYPE_VARIANT;
}

gboolean
plugin_init(system_ui_data *data)
{
  g_assert(data);

  sui = data;

  if (!_setup_dbus())
  {
    SYSTEMUI_CRITICAL("Failed to setup dbus properly, failing...");
    return FALSE;
  }

  systemui_add_handler(SYSTEMUI_SPLASHSCREEN_OPEN_REQ,
                       splashscreen_open_handler, sui);
  systemui_add_handler(SYSTEMUI_SPLASHSCREEN_CLOSE_REQ,
                       splashscreen_close_handler, sui);

  return TRUE;
}

void
plugin_close(system_ui_data *data)
{
  gchar *dsme_sig = _dsme_shutdown_ind_sig();

  systemui_remove_handler(SYSTEMUI_SPLASHSCREEN_OPEN_REQ, data);
  systemui_remove_handler(SYSTEMUI_SPLASHSCREEN_CLOSE_REQ, data);

  dbus_bus_remove_match(sui->system_bus, dsme_sig, &sui->dbuserror);
  g_free(dsme_sig);
  dbus_connection_remove_filter(sui->system_bus, _splashscreen_dbus_filter_cb,
                                sui);
  if (conn)
  {
    dbus_bus_remove_match(conn, HD_APP_MGR_DBUS_MATCH, &sui->dbuserror);
    dbus_connection_remove_filter(conn, _splashscreen_dbus_filter_cb, NULL);
    dbus_connection_unref(conn);
    conn = NULL;
  }
}

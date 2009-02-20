/*
 * This file is part of hildon-home
 *
 * Copyright (C) 2009 Nokia Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <hildon/hildon.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <gconf/gconf-client.h>

#include "hd-background-helper.h"

#include "hd-backgrounds.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

#define CACHED_DIR        ".backgrounds"
#define BACKGROUND_CACHED CACHED_DIR "/background-%u.png"
#define THUMBNAIL_CACHED  CACHED_DIR "/thumbnail-%u.png"
#define INFO_CACHED       CACHED_DIR "/background-%u.info"

/* Background GConf key */
#define GCONF_DIR                 "/apps/osso/hildon-desktop/views"
#define GCONF_BACKGROUND_KEY      GCONF_DIR "/%u/bg-image"
#define GCONF_CURRENT_DESKTOP_KEY "/apps/osso/hildon-desktop/views/current"

/* Background from theme */
#define CURRENT_THEME_DIR            "/etc/hildon/theme"
#define CURRENT_BACKGROUNDS_DESKTOP  CURRENT_THEME_DIR "/backgrounds/theme_bg.desktop"
#define DEFAULT_THEME_DIR            "/usr/share/themes/default"
#define DEFAULT_BACKGROUNDS_DESKTOP  DEFAULT_THEME_DIR "/backgrounds/theme_bg.desktop"
#define BACKGROUNDS_DESKTOP_KEY_FILE "X-File%u"

#define HD_BACKGROUNDS_GET_PRIVATE(object) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object), HD_TYPE_BACKGROUNDS, HDBackgroundsPrivate))

typedef struct
{
  gchar *uri;
  guint view;
  GCancellable *cancellable;
  GtkWidget *cancel_note;
  gboolean write_to_gconf;
} HDBackgroundData;

struct _HDBackgroundsPrivate
{
  GConfClient *gconf_client;

  HDBackgroundData *current_data;

  guint bg_image_notify[4];
  gchar *bg_image[4];

  GMutex *mutex;
  GQueue *queue;
};

static void check_queue (HDBackgrounds *backgrounds);

G_DEFINE_TYPE (HDBackgrounds, hd_backgrounds, G_TYPE_OBJECT);

static void
finish_image_caching (HDBackgrounds *backgrounds)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;

  /* Remove timeout */
  if (priv->current_data->cancel_note)
    priv->current_data->cancel_note = (gtk_widget_destroy (priv->current_data->cancel_note),
                                       NULL);

  g_mutex_lock (priv->mutex);
  priv->current_data = (g_free (priv->current_data), NULL);
  g_mutex_unlock (priv->mutex);

  check_queue (backgrounds);
}

static void
save_cached_background_cb (GFile         *file,
                           GAsyncResult  *res,
                           HDBackgrounds *backgrounds)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;
  GError *error = NULL;

  hd_background_helper_save_pixbuf_finish (file, res, &error);

  if (error)
    {
      g_debug ("%s, Could not save cached pixbuf. %s",
               __FUNCTION__,
               error->message);

      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE))
        {
          GtkWidget *note;

          note = hildon_note_new_information (NULL,
                                              dgettext ("hildon-common-strings",
                                                        "sfil_ni_not_enough_memory"));
          gtk_widget_show (note);
        }
      g_clear_error (&error);
    }
  else
    {
      guint view = priv->current_data->view;

      g_free (priv->bg_image[view - 1]);
      priv->bg_image[view - 1] = priv->current_data->uri;

      if (priv->current_data->write_to_gconf)
        {
          gchar *gconf_key;
          GError *error = NULL;

          /* Store background to GConf */
          gconf_key = g_strdup_printf (GCONF_BACKGROUND_KEY, view);
          gconf_client_set_string (priv->gconf_client,
                                   gconf_key,
                                   priv->bg_image[view - 1],
                                   &error);

          if (error)
            {
              g_debug ("%s. Could not set background in GConf for view %u. %s",
                       __FUNCTION__,
                       view,
                       error->message);
              g_error_free (error);
            }

          g_free (gconf_key);
        }
    }

  finish_image_caching (backgrounds);
}

static void
read_pixbuf_cb (GFile         *file,
                GAsyncResult  *res,
                HDBackgrounds *backgrounds)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;
  GdkPixbuf *pixbuf;
  GError *error = NULL;
  gchar *cached_filename;
  GFile *cached_file;

  pixbuf = hd_background_helper_read_pixbuf_finish (file,
                                                    res,
                                                    &error);

  if (!pixbuf)
    {
      if (g_error_matches (error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE))
        {
          GtkWidget *note;

          note = hildon_note_new_information (NULL,
                                              dgettext ("hildon-common-strings",
                                                        "ckct_ni_unable_to_open_file_corrupted"));
          gtk_widget_show (note);
        }
      g_clear_error (&error);

      finish_image_caching (backgrounds);
      return;
    }

  cached_filename = g_strdup_printf ("%s/" BACKGROUND_CACHED,
                                     g_get_home_dir (),
                                     priv->current_data->view);
  cached_file = g_file_new_for_path (cached_filename);

  hd_background_helper_save_pixbuf_async (cached_file,
                                          pixbuf,
                                          G_PRIORITY_DEFAULT,
                                          priv->current_data->cancellable,
                                          (GAsyncReadyCallback) save_cached_background_cb,
                                          backgrounds);

  g_free (cached_filename);
  g_object_unref (cached_file);
  g_object_unref (pixbuf);
}

static void
checked_cache_cb (GFile         *file,
                  GAsyncResult  *res,
                  HDBackgrounds *backgrounds)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;
  GError *error = NULL;

  if (!hd_background_helper_check_cache_finish (file, res, &error))
    {
      hd_background_helper_read_pixbuf_async (file,
                                              G_PRIORITY_DEFAULT,
                                              priv->current_data->cancellable,
                                              (GAsyncReadyCallback) read_pixbuf_cb,
                                              backgrounds);
    }
  else
    {
      finish_image_caching (backgrounds);
    }
}

static void
check_queue (HDBackgrounds *backgrounds)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;
  HDBackgroundData *data = NULL;

  g_mutex_lock (priv->mutex);
  if (!priv->current_data)
    {
      data = g_queue_pop_head (priv->queue);
      priv->current_data = data;
    }
  g_mutex_unlock (priv->mutex);

  if (data)
    {
      if (g_strcmp0 (priv->bg_image[data->view - 1],
                     data->uri) == 0)
        {
          finish_image_caching (backgrounds);
        }
      else
        {
          GFile *file;

          if (g_path_is_absolute (data->uri))
            file = g_file_new_for_path (data->uri);
          else
            file = g_file_new_for_uri (data->uri);

          hd_background_helper_check_cache_async (file,
                                                  data->view,
                                                  G_PRIORITY_DEFAULT,
                                                  data->cancellable,
                                                  (GAsyncReadyCallback) checked_cache_cb,
                                                  backgrounds);

          g_object_unref (file);
        }
    }
}

static void
cancel_response_cb (GtkDialog    *dialog,
                    gint          response_id,
                    GCancellable *cancellable)
{
  if (response_id == GTK_RESPONSE_CANCEL)
    g_cancellable_cancel (cancellable);
}

static void
create_cached_background (HDBackgrounds *backgrounds,
                          guint          view,
                          const gchar   *uri,
                          gboolean       display_note,
                          gboolean       store_to_gconf)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;
  HDBackgroundData *data = g_new0 (HDBackgroundData, 1);

  data->uri = g_strdup (uri);
  data->view = view;

  data->cancellable = g_cancellable_new ();
  if (display_note)
    {
      data->cancel_note = hildon_note_new_cancel_with_progress_bar (NULL,
                                                                    dgettext ("hildon-common-strings",
                                                                              "ckdg_pb_updating"),
                                                                    NULL);
      g_signal_connect_object (data->cancel_note, "response",
                               G_CALLBACK (cancel_response_cb), data->cancellable, 0);
      gtk_widget_show (data->cancel_note);
    }

  g_mutex_lock (priv->mutex);
  g_queue_push_tail (priv->queue, data);
  g_mutex_unlock (priv->mutex);

  check_queue (backgrounds);
}

static gchar *
get_background_for_view_from_theme (HDBackgrounds *backgrounds,
                                    guint          view,
                                    const gchar   *backgrounds_desktop)
{
  gchar *key;
  GKeyFile *key_file;
  gchar *bg_image = NULL;
  GError *error = NULL;

  key = g_strdup_printf (BACKGROUNDS_DESKTOP_KEY_FILE, view);

  key_file = g_key_file_new ();
  if (!g_key_file_load_from_file (key_file,
                                  backgrounds_desktop,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      g_debug ("%s. Could not load background defintion for theme %s. %s",
               __FUNCTION__,
               backgrounds_desktop,
               error->message);
      g_error_free (error);
      goto cleanup;
    }

  bg_image = g_key_file_get_string (key_file,
                                    G_KEY_FILE_DESKTOP_GROUP,
                                    key,
                                    &error);

  if (error)
    {
      g_debug ("%s. Could not load background defintion for theme %s. %s",
               __FUNCTION__,
               backgrounds_desktop,
               error->message);
      g_error_free (error);
    }

cleanup:
  g_free (key);
  g_key_file_free (key_file);
  return bg_image;
}

static gchar *
get_background_for_view (HDBackgrounds *backgrounds,
                         guint          view)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;
  gchar *gconf_key;
  gchar *bg_image;
  GError *error = NULL;

  /* First try to get the background image from GConf */
  gconf_key = g_strdup_printf (GCONF_BACKGROUND_KEY, view);
  bg_image = gconf_client_get_string (priv->gconf_client,
                                      gconf_key,
                                      &error);

  if (error)
    {
      g_debug ("%s. Could not get background from GConf for view %u. %s",
               __FUNCTION__,
               view,
               error->message);
      g_error_free (error);
    }

  g_free (gconf_key);

  /* Return bg image if it was stored in GConf */
  if (bg_image)
    return bg_image;

  /* Fallback to current theme */
  bg_image = get_background_for_view_from_theme (backgrounds,
                                                 view,
                                                 CURRENT_BACKGROUNDS_DESKTOP);
  if (bg_image)
    return bg_image;


  /* Fallback to current theme */
  bg_image = get_background_for_view_from_theme (backgrounds,
                                                 view,
                                                 CURRENT_BACKGROUNDS_DESKTOP);
  if (!bg_image)
    {
      g_warning ("%s. Could not get any background for view %u",
                 __FUNCTION__,
                 view);
    }

  return bg_image;
}

static void 
gconf_bgimage_notify (GConfClient *client,
                      guint        cnxn_id,
                      GConfEntry  *entry,
                      gpointer     user_data)
{
  HDBackgrounds *backgrounds = hd_backgrounds_get ();
  gchar *bg_image;

  bg_image = get_background_for_view (backgrounds,
                                      GPOINTER_TO_UINT (user_data));
  create_cached_background (backgrounds,
                            GPOINTER_TO_UINT (user_data),
                            bg_image,
                            TRUE,
                            FALSE);
  g_free (bg_image);
}

void
hd_backgrounds_startup (HDBackgrounds *backgrounds)
{
  HDBackgroundsPrivate *priv = backgrounds->priv;
  gchar *cached_dir;
  guint current_view, i;
  gchar *bg_image;
  GError *error = NULL;

  cached_dir = g_strdup_printf ("%s/" CACHED_DIR,
                                g_get_home_dir ());
  if (g_mkdir (cached_dir,
               S_IRUSR | S_IWUSR | S_IXUSR |
               S_IRGRP | S_IXGRP | 
               S_IROTH | S_IXOTH))
    {
      g_warning ("%s, Could not make dir %s",
                 __FUNCTION__,
                 cached_dir);
    }
  g_free (cached_dir);

  gconf_client_add_dir (priv->gconf_client,
                        GCONF_DIR,
                        GCONF_CLIENT_PRELOAD_NONE,
                        &error);
  if (error)
    {
      g_debug ("%s. Could not add dir %s in GConf. %s",
               __FUNCTION__,
               GCONF_DIR,
               error->message);
      g_clear_error (&error);
    }


  current_view = gconf_client_get_int (priv->gconf_client,
                                       GCONF_CURRENT_DESKTOP_KEY,
                                       &error);

  if (error)
    {
      g_debug ("%s. Could not get current view. %s",
               __FUNCTION__,
               error->message);
      g_clear_error (&error);
    }

  current_view = CLAMP (current_view, 1, 4);

  /* Listen to GConf changes */
  for (i = 0; i < 4; i++)
    {
      gchar *gconf_key;

      gconf_key = g_strdup_printf (GCONF_BACKGROUND_KEY, i + 1);
      priv->bg_image_notify[i] = gconf_client_notify_add (priv->gconf_client,
                                                          gconf_key,
                                                          (GConfClientNotifyFunc) gconf_bgimage_notify,
                                                          GUINT_TO_POINTER (i + 1),
                                                          NULL,
                                                          &error);
      if (error)
        {
          g_warning ("%s. Could not add notification to GConf %s. %s",
                     __FUNCTION__,
                     gconf_key,
                     error->message);
          g_clear_error (&error);
        }
      g_free (gconf_key);
    }

  /* Update cache for current view */
  bg_image = get_background_for_view (backgrounds,
                                      current_view);
  create_cached_background (backgrounds,
                            current_view,
                            bg_image,
                            FALSE,
                            FALSE);

  /* Update cache for other views */
  for (i = 1; i <= 4; i++)
    {
      if (i != current_view)
        {
          bg_image = get_background_for_view (backgrounds,
                                              i);
          create_cached_background (backgrounds,
                                    i,
                                    bg_image,
                                    FALSE,
                                    FALSE);
        }
    }
}

static void
hd_backgrounds_init (HDBackgrounds *backgrounds)
{
  HDBackgroundsPrivate *priv;

  backgrounds->priv = HD_BACKGROUNDS_GET_PRIVATE (backgrounds);
  priv = backgrounds->priv;

  priv->gconf_client = gconf_client_get_default ();

  priv->mutex = g_mutex_new ();
  priv->queue = g_queue_new ();
}

static void
hd_backgrounds_dipose (GObject *object)
{
  HDBackgroundsPrivate *priv = HD_BACKGROUNDS (object)->priv;
  guint i;

  if (priv->gconf_client)
    {
      for (i = 0; i < 4; i++)
        {
          if (priv->bg_image_notify[i])
            priv->bg_image_notify[i] = (gconf_client_notify_remove (priv->gconf_client,
                                                                    priv->bg_image_notify[i]), 0);
          priv->bg_image[i] = (g_free (priv->bg_image[i]), NULL);
        }
      priv->gconf_client = (g_object_unref (priv->gconf_client), NULL);
    }

  if (priv->mutex)
    priv->mutex = (g_mutex_free (priv->mutex), NULL);

  if (priv->queue)
    priv->queue = (g_queue_free (priv->queue), NULL);

  G_OBJECT_CLASS (hd_backgrounds_parent_class)->dispose (object);
}

static void
hd_backgrounds_class_init (HDBackgroundsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = hd_backgrounds_dipose;

  g_type_class_add_private (klass, sizeof (HDBackgroundsPrivate));
}

/* Retuns the singleton HDBackgrounds instance. Should not be refed or unrefed */
HDBackgrounds *
hd_backgrounds_get (void)
{
  static HDBackgrounds *backgrounds = NULL;

  if (G_UNLIKELY (!backgrounds))
    {
      backgrounds = g_object_new (HD_TYPE_BACKGROUNDS, NULL);
    }

  return backgrounds;
}

void
hd_backgrounds_set_background (HDBackgrounds *backgrounds,
                               guint          view,
                               const gchar   *uri)
{
  gchar *bg_image;

  if (g_path_is_absolute (uri))
    {
      bg_image = g_strdup (uri);
    }
  else
    {
      GFile *file = g_file_new_for_uri (uri);

      if (g_file_is_native (file))
        bg_image = g_file_get_path (file);
      else
        bg_image = g_file_get_uri (file);

      g_object_unref (file);
    }

  create_cached_background (backgrounds,
                            view,
                            bg_image,
                            TRUE,
                            TRUE);

   g_free (bg_image);
}

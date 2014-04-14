#include <gst/gst.h>
#include <gst/pbutils/encoding-profile.h>
#include <gst/pbutils/encoding-target.h>

static GstEncodingProfile *
encodingProfile (const char *file, const char *name)
{
  GstEncodingTarget *target = gst_encoding_target_load_from_file (file, NULL);
  if (!target) {
    return NULL;
  }

  GstEncodingProfile *profile = gst_encoding_target_get_profile (target, name);

  gst_encoding_target_unref (target);

  return profile;
}

gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  gchar *debug = NULL;
  GError *err = NULL;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit ((GMainLoop *) data);
      break;

    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug);
      g_error ("Error: %s\n", err->message);

      g_main_loop_quit ((GMainLoop *) data);

      if (err) {
        g_error_free (err);
      }

      if (debug) {
        g_free (debug);
      }

      break;

    default:
      break;
  }

  return TRUE;
}

gboolean
start_capture (GstElement * bin)
{
  g_signal_emit_by_name (bin, "start-capture", NULL);
  return FALSE;
}

gboolean
stop_capture (GstElement * bin)
{
  g_signal_emit_by_name (bin, "stop-capture", NULL);
  return FALSE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop = g_main_loop_new (NULL, FALSE);
  gst_init (&argc, &argv);

  GstElement *bin = gst_element_factory_make ("camerabin", NULL);
  GstElement *src = gst_element_factory_make ("droidcamsrc", NULL);
  GstElement *sink = gst_element_factory_make ("droideglsink", NULL);
  GstEncodingProfile *video = encodingProfile ("video.gep", "video-profile");
  GstEncodingProfile *image = encodingProfile ("image.gep", "image-profile");

  g_object_set (bin, "camera-source", src, "viewfinder-sink", sink, "flags",
      0x00000001 | 0x00000002 | 0x00000004 | 0x00000008, "image-profile", image,
      "video-profile", video, "mode", 2, NULL);

  GstBus *bus = gst_element_get_bus (bin);
  gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  g_timeout_add_seconds (3, (GSourceFunc) start_capture, (gpointer) bin);
  g_timeout_add_seconds (10, (GSourceFunc) stop_capture, (gpointer) bin);
  g_timeout_add_seconds (15, (GSourceFunc) g_main_loop_quit, (gpointer) loop);

  gst_element_set_state (bin, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (bin, GST_STATE_NULL);
  return 0;
}

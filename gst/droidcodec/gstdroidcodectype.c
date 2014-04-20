/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstdroidcodectype.h"
#include "plugin.h"

typedef struct _GstDroidCodecType GstDroidCodecType;

static gboolean
mpeg4v (GstStructure * s)
{
  gint val;

  return gst_structure_get_int (s, "mpegversion", &val) && val == 4;
}

static gboolean
h264 (GstStructure * s)
{
  const char *alignment = gst_structure_get_string (s, "alignment");
  const char *format = gst_structure_get_string (s, "stream-format");

  return alignment && format && !g_strcmp0 (alignment, "au")
      && !g_strcmp0 (format, "byte-stream");
}

static gboolean
h264_enc (GstStructure * s)
{
  const char *alignment = gst_structure_get_string (s, "alignment");
  const char *format = gst_structure_get_string (s, "stream-format");

  if (alignment && g_strcmp0 (alignment, "au")) {
    return FALSE;
  }

  if (format && g_strcmp0 (format, "byte-stream")) {
    return FALSE;
  }

  return TRUE;
}

void
h264_compliment (GstCaps * caps)
{
  gst_caps_set_simple (caps, "alignment", G_TYPE_STRING, "au",
      "stream-format", G_TYPE_STRING, "byte-stream", NULL);
}

struct _GstDroidCodecType
{
  GstDroidCodecTypeType type;
  const gchar *media_type;
  const gchar *codec_type;
    gboolean (*verify) (GstStructure * s);
  void (*compliment) (GstCaps * caps);
  const gchar *caps;
  gboolean in_stream_headers;
};

GstDroidCodecType types[] = {
  /* decoders */
  {GST_DROID_CODEC_DECODER, "video/mpeg", GST_DROID_CODEC_TYPE_MPEG4VIDEO_DEC,
        mpeg4v, NULL,
      "video/mpeg, mpegversion=4", FALSE},
  {GST_DROID_CODEC_DECODER, "video/x-h264", GST_DROID_CODEC_TYPE_AVC_DEC, h264,
      NULL, "video/x-h264, alignment=au, stream-format=byte-stream", FALSE},
  {GST_DROID_CODEC_DECODER, "video/x-h263", GST_DROID_CODEC_TYPE_H263_DEC, NULL,
      NULL, "video/x-h263", FALSE},
  {GST_DROID_CODEC_DECODER, "video/x-divx", GST_DROID_CODEC_TYPE_DIVX_DEC, NULL,
      NULL, "video/x-divx", FALSE},

  /* encoders */
  {GST_DROID_CODEC_ENCODER, "video/mpeg", GST_DROID_CODEC_TYPE_MPEG4VIDEO_ENC,
        mpeg4v,
      NULL, "video/mpeg, mpegversion=4, systemstream=false", FALSE},
  {GST_DROID_CODEC_ENCODER, "video/x-h264", GST_DROID_CODEC_TYPE_AVC_ENC,
        h264_enc, h264_compliment,
      "video/x-h264, alignment=au, stream-format=byte-stream", TRUE},
};

const gchar *
gst_droid_codec_type_from_caps (GstCaps * caps, GstDroidCodecTypeType type)
{
  int x = 0;
  int len = G_N_ELEMENTS (types);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (s);

  for (x = 0; x < len; x++) {
    if (types[x].type != type) {
      continue;
    }

    gboolean is_equal = g_strcmp0 (types[x].media_type, name) == 0;
    if ((is_equal && !types[x].verify) || (is_equal && types[x].verify
            && types[x].verify (s))) {
      return types[x].codec_type;
    }
  }

  return NULL;
}

GstCaps *
gst_droid_codec_type_all_caps (GstDroidCodecTypeType type)
{
  GstCaps *caps = gst_caps_new_empty ();
  int x = 0;
  int len = G_N_ELEMENTS (types);

  for (x = 0; x < len; x++) {
    if (types[x].type != type) {
      continue;
    }

    gchar *file = gst_droid_codec_type_get_path (types[x].codec_type);

    if (g_file_test (file, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS)) {
      GstStructure *s = gst_structure_new_from_string (types[x].caps);
      caps = gst_caps_merge_structure (caps, s);
    }

    g_free (file);
  }

  GST_INFO ("caps %" GST_PTR_FORMAT, caps);

  return caps;
}

GstDroidCodecTypeType
gst_droid_codec_type_get_type (const gchar * type)
{
  int x = 0;
  int len = G_N_ELEMENTS (types);

  for (x = 0; x < len; x++) {
    if (!g_strcmp0 (type, types[x].codec_type)) {
      return types[x].type;
    }
  }

  /* should not happen */
  return -1;
}

gchar *
gst_droid_codec_type_get_path (const gchar * type)
{
  gchar *file_name = g_strdup_printf ("%s.conf", type);
  gchar *file =
      g_build_path ("/", SYSCONFDIR, "gst-droid", "droidcodec.d", file_name,
      NULL);

  g_free (file_name);

  return file;
}

void
gst_droid_codec_type_compliment_caps (const gchar * type, GstCaps * caps)
{
  int x = 0;
  int len = G_N_ELEMENTS (types);

  for (x = 0; x < len; x++) {
    if (!g_strcmp0 (type, types[x].codec_type)) {
      if (types[x].compliment) {
        types[x].compliment (caps);
      }

      return;
    }
  }
}

gboolean
gst_droid_codec_type_in_stream_headers (const gchar * type, gboolean * result)
{
  int x = 0;
  int len = G_N_ELEMENTS (types);

  for (x = 0; x < len; x++) {
    if (!g_strcmp0 (type, types[x].codec_type)) {
      *result = types[x].in_stream_headers;
      return TRUE;
    }
  }

  return FALSE;
}

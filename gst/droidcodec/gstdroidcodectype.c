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
#include <string.h>
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

  return alignment && format && !strcmp (alignment, "au")
      && !strcmp (format, "byte-stream");
}

struct _GstDroidCodecType
{
  GstDroidCodecTypeType type;
  const gchar *media_type;
  const gchar *codec_type;
    gboolean (*verify) (GstStructure * s);
  const gchar *caps;
};

GstDroidCodecType types[] = {
  {GST_DROID_CODEC_DECODER, "video/mpeg", "mpeg4videodec", mpeg4v,
      "video/mpeg, mpegversion=4"},
  {GST_DROID_CODEC_DECODER, "video/x-h264", "h264decode", h264,
      "video/x-h264, alignment=au, stream-format=byte-stream"},
  {GST_DROID_CODEC_DECODER, "video/x-h263", "h263decode", NULL, "video/x-h263"},
  {GST_DROID_CODEC_DECODER, "video/x-divx", "divxdecode", NULL, "video/x-divx"},
};

const gchar *
gst_droid_codec_type_from_caps (GstCaps * caps)
{
  int x = 0;
  int len = sizeof (types) / sizeof (types[0]);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (s);

  for (x = 0; x < len; x++) {
    gboolean is_equal = strcmp (types[x].media_type, name) == 0;
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
  int len = sizeof (types) / sizeof (types[0]);

  for (x = 0; x < len; x++) {
    if (types[x].type != type) {
      continue;
    }

    gchar *file = g_strdup_printf ("%s/%s.conf", DROID_CODEC_CONFIG_DIR,
        types[x].codec_type);
    if (g_file_test (file, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS)) {
      GstStructure *s = gst_structure_new_from_string (types[x].caps);
      caps = gst_caps_merge_structure (caps, s);
    }

    g_free (file);
  }

  return caps;
}

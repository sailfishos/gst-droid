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

typedef struct _GstDroidCodecType GstDroidCodecType;

static gboolean
mpeg4v (GstStructure * s)
{
  gint val;

  return gst_structure_get_int (s, "mpegversion", &val) && val == 4;
}

struct _GstDroidCodecType
{
  const gchar *media_type;
  const gchar *codec_type;
    gboolean (*verify) (GstStructure * s);
};

GstDroidCodecType types[] = {
  {"video/mpeg", "mpeg4videodec", mpeg4v},
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

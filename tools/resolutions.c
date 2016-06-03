/*
 * gst-droid
 *
 * Copyright (C) 2016 Jolla LTD.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "common.h"
#include <math.h>
#include <float.h>

struct Ratio
{
  gfloat value;
  const char *name;
} Ratios[] = {
  {
  1.3333333333f, "4:3"}, {
  1.7777777778f, "16:9"}, {
  1.5, "3:2"}, {
  1.6666666667f, "16:10"},
      /* What are those? */
      /*  {1.8, "9:5"}, */
};

void
print (int w, int h)
{
  char *name = NULL;
  int x;
  for (x = 0; x < sizeof (Ratios) / sizeof (struct Ratio); x++) {
    if (fabsf ((float) w / h - Ratios[x].value) < FLT_EPSILON) {
      name = g_strdup (Ratios[x].name);
      break;
    }
  }

  if (!name) {
    name = g_strdup_printf ("UNK (%.6f)", (float) w / h);
  }

  float mp = w * h / 1000000.0;
  g_print ("%.2f %ix%i %s\n", mp, w, h, name);
  g_free (name);
}

gboolean
dump_resolution (GstStructure * s)
{
  const GValue *w = gst_structure_get_value (s, "width");
  const GValue *h = gst_structure_get_value (s, "height");
  gboolean w_list = GST_VALUE_HOLDS_LIST (w);
  gboolean h_list = GST_VALUE_HOLDS_LIST (h);

  if (!w_list && !h_list) {
    print (g_value_get_int (w), g_value_get_int (h));
  } else if (w_list && !h_list) {
    int height = g_value_get_int (h);
    int x;
    for (x = 0; x < gst_value_list_get_size (w); x++) {
      print (g_value_get_int (gst_value_list_get_value (w, x)), height);
    }
  } else if (h_list && !w_list) {
    int width = g_value_get_int (w);
    int x;
    for (x = 0; x < gst_value_list_get_size (h); x++) {
      print (width, g_value_get_int (gst_value_list_get_value (h, x)));
    }
  } else {
    /* Both are lists :/ */
    int x, i;
    for (x = 0; x < gst_value_list_get_size (w); x++) {
      const GValue *width = gst_value_list_get_value (w, x);

      for (i = 0; i < gst_value_list_get_size (h); i++) {
        const GValue *height = gst_value_list_get_value (h, i);
        print (g_value_get_int (width), g_value_get_int (height));
      }
    }
  }


  return TRUE;

  /*
     gint w = 0, h = 0;
     if (!gst_structure_get_int(s, "width", &w) ||
     !gst_structure_get_int(s, "height", &h)) {
     ret = FALSE;
     goto out;
     }

     g_print("%ix%i\n", w, h);
   */
}

gboolean
dump_pad (GstElement * src, const gchar * pad_name, const gchar * name)
{
  gboolean ret = TRUE;
  GstPad *pad = gst_element_get_static_pad (src, pad_name);
  if (!pad) {
    return FALSE;
  }

  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  if (!caps) {
    ret = FALSE;
    goto out;
  }

  g_print (name, pad_name);

  int x;
  for (x = 0; x < gst_caps_get_size (caps); x++) {
    GstStructure *s = gst_caps_get_structure (caps, x);
    if (!dump_resolution (s)) {
      ret = FALSE;
      goto out;
    }
  }

  g_print ("== Done\n");

out:
  if (caps) {
    gst_caps_unref (caps);
  }

  gst_object_unref (pad);
  return ret;
}

gboolean
dump (Device dev, const char *name, gboolean deinit)
{
  Common *common = common_init (0, NULL, "camerabin");
  if (!common) {
    return FALSE;
  }

  gboolean ret = TRUE;

  common_set_device_mode (common, dev, IMAGE);
  gst_element_set_state (GST_ELEMENT (common->bin), GST_STATE_PAUSED);

  ret = dump_pad (common->cam_src, "vfsrc", name) &&
      dump_pad (common->cam_src, "imgsrc", name) &&
      dump_pad (common->cam_src, "vidsrc", name);

  gst_element_set_state (GST_ELEMENT (common->bin), GST_STATE_NULL);

  common_destroy (common, deinit);

  return ret;
}

int
main (int argc, char *argv[])
{
  if (!dump (PRIMARY, "primary %s:\n", FALSE) ||
      !dump (SECONDARY, "secondary %s:\n", TRUE)) {
    return 1;
  }

  return 0;
}

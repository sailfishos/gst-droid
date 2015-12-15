/*
 * gst-droid
 *
 * Copyright (C) 2015 Jolla LTD.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "gstdroidquery.h"

GstQuery *
gst_droid_query_new_video_color_format ()
{
  GstQuery *query;
  GstStructure *structure;

  structure = gst_structure_new_empty (GST_DROID_VIDEO_COLOR_FORMAT_QUERY_NAME);

  query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);

  return query;
}

void
gst_droid_query_set_video_color_format (GstQuery * query, gint format)
{
  GstStructure *structure;

  structure = gst_query_writable_structure (query);

  gst_structure_set (structure, "color-format", G_TYPE_INT, format, NULL);
}

gboolean
gst_droid_query_parse_video_color_format (GstQuery * query, gint * format)
{
  GstStructure *structure;

  structure = gst_query_get_structure (query);

  return gst_structure_get_int (structure, "color-format", format);
}

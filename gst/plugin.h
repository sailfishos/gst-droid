/*
 * gst-droid
 *
 * Copyright (C) 2014-2015 Mohammed Sameer
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

#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

static inline gboolean
gst_droid_camera_startup_logging_enabled (void)
{
  return g_getenv ("CAMERA_STARTUP_LOG") != NULL;
}

static inline gint64
gst_droid_camera_startup_mono_ms (void)
{
  return g_get_monotonic_time () / G_TIME_SPAN_MILLISECOND;
}

G_END_DECLS

#endif /* __PLUGIN_H__ */

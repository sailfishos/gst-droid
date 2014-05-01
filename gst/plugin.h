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

#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_droid_dec_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_droid_enc_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_droid_codec_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_droid_eglsink_debug);

G_END_DECLS

#endif /* __PLUGIN_H__ */

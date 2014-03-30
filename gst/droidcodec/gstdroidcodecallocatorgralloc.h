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

#ifndef __GST_DROID_CODEC_ALLOCATOR_GRALLOC_H__
#define __GST_DROID_CODEC_ALLOCATOR_GRALLOC_H__

#include <gst/gst.h>
#include "gstdroidcodec.h"

G_BEGIN_DECLS

#define GST_ALLOCATOR_DROID_CODEC_GRALLOC                   "droidcodecgralloc"

GstAllocator * gst_droid_codec_allocator_gralloc_new (GstDroidComponentPort * port);
OMX_BUFFERHEADERTYPE * gst_droid_codec_gralloc_allocator_get_omx_buffer (GstMemory * mem);

G_END_DECLS

#endif /* __GST_DROID_CODEC_ALLOCATOR_GRALLOC_H__ */

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

#ifndef __ENCODER_PARAMS_H__
#define __ENCODER_PARAMS_H__

#include <glib.h>
#include "OMX_Video.h"

OMX_VIDEO_MPEG4PROFILETYPE gst_encoder_params_get_mpeg4_profile (const gchar * profile);
OMX_VIDEO_MPEG4LEVELTYPE gst_encoder_params_get_mpeg4_level (const gchar * level);
OMX_VIDEO_AVCPROFILETYPE gst_encoder_params_get_avc_profile (const gchar * profile);
OMX_VIDEO_AVCLEVELTYPE gst_encoder_params_get_avc_level (const gchar * level);

#endif /* __ENCODER_PARAMS_H__ */

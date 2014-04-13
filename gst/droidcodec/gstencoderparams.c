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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstencoderparams.h"
#include <string.h>

typedef struct
{
  gchar *str;
  int omx;
} Entry;

/* Those tables were generated from gst-omx */
Entry Mpeg4Profiles[] = {
  {"simple", OMX_VIDEO_MPEG4ProfileSimple},
  {"simple-scalable", OMX_VIDEO_MPEG4ProfileSimpleScalable},
  {"core", OMX_VIDEO_MPEG4ProfileCore},
  {"main", OMX_VIDEO_MPEG4ProfileMain},
  {"n-bit", OMX_VIDEO_MPEG4ProfileNbit},
  {"scalable", OMX_VIDEO_MPEG4ProfileScalableTexture},
  {"simple-face", OMX_VIDEO_MPEG4ProfileSimpleFace},
  {"simple-fba", OMX_VIDEO_MPEG4ProfileSimpleFBA},
  {"basic-animated-texture", OMX_VIDEO_MPEG4ProfileBasicAnimated},
  {"hybrid", OMX_VIDEO_MPEG4ProfileHybrid},
  {"advanced-real-time-simple", OMX_VIDEO_MPEG4ProfileAdvancedRealTime},
  {"core-scalable", OMX_VIDEO_MPEG4ProfileCoreScalable},
  {"advanced-coding-efficiency", OMX_VIDEO_MPEG4ProfileAdvancedCoding},
  {"advanced-core", OMX_VIDEO_MPEG4ProfileAdvancedCore},
  {"advanced-scalable-texture", OMX_VIDEO_MPEG4ProfileAdvancedScalable},
  {"advanced-simple", OMX_VIDEO_MPEG4ProfileAdvancedSimple},
};

Entry Mpeg4Levels[] = {
  {"0", OMX_VIDEO_MPEG4Level0},
  {"0b", OMX_VIDEO_MPEG4Level0b},
  {"1", OMX_VIDEO_MPEG4Level1},
  {"2", OMX_VIDEO_MPEG4Level2},
  {"3", OMX_VIDEO_MPEG4Level3},
  {"4", OMX_VIDEO_MPEG4Level4},
  {"4a", OMX_VIDEO_MPEG4Level4a},
  {"5", OMX_VIDEO_MPEG4Level5},
};

Entry AvcProfiles[] = {
  {"baseline", OMX_VIDEO_AVCProfileBaseline},
  {"main", OMX_VIDEO_AVCProfileMain},
  {"extended", OMX_VIDEO_AVCProfileExtended},
  {"high", OMX_VIDEO_AVCProfileHigh},
  {"high-10", OMX_VIDEO_AVCProfileHigh10},
  {"high-4:2:2", OMX_VIDEO_AVCProfileHigh422},
  {"high-4:4:4", OMX_VIDEO_AVCProfileHigh444},
};

Entry AvcLevels[] = {
  {"1", OMX_VIDEO_AVCLevel1},
  {"1b", OMX_VIDEO_AVCLevel1b},
  {"1.1", OMX_VIDEO_AVCLevel11},
  {"1.2", OMX_VIDEO_AVCLevel12},
  {"1.3", OMX_VIDEO_AVCLevel13},
  {"2", OMX_VIDEO_AVCLevel2},
  {"2.1", OMX_VIDEO_AVCLevel21},
  {"2.2", OMX_VIDEO_AVCLevel22},
  {"3", OMX_VIDEO_AVCLevel3},
  {"3.1", OMX_VIDEO_AVCLevel31},
  {"3.2", OMX_VIDEO_AVCLevel32},
  {"4", OMX_VIDEO_AVCLevel4},
  {"4.1", OMX_VIDEO_AVCLevel41},
  {"4.2", OMX_VIDEO_AVCLevel42},
  {"5", OMX_VIDEO_AVCLevel5},
  {"5.1", OMX_VIDEO_AVCLevel51},
};

int
find_in_array (Entry entries[], const gchar * str)
{
  int x;
  int len = sizeof (entries) / sizeof (entries[0]);

  if (!str) {
    return -1;
  }

  for (x = 0; x < len; x++) {
    if (!strcmp (str, entries[x].str)) {
      return entries[x].omx;
    }
  }

  return -1;
}

OMX_VIDEO_MPEG4PROFILETYPE
gst_encoder_params_get_mpeg4_profile (const gchar * profile)
{
  return find_in_array (Mpeg4Profiles, profile);
}

OMX_VIDEO_MPEG4LEVELTYPE
gst_encoder_params_get_mpeg4_level (const gchar * level)
{
  return find_in_array (Mpeg4Levels, level);
}

OMX_VIDEO_AVCPROFILETYPE
gst_encoder_params_get_avc_profile (const gchar * profile)
{
  return find_in_array (AvcProfiles, profile);
}

OMX_VIDEO_AVCLEVELTYPE
gst_encoder_params_get_avc_level (const gchar * level)
{
  return find_in_array (AvcLevels, level);
}

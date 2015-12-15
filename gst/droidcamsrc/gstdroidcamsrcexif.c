/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
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

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroidcamsrcexif.h"
#include <string.h>

#include <gst/base/gstbytereader.h>
#include <gst/tag/tag.h>
#include <libexif/exif-data.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

#if 0
const guchar marker[2] = { 0xFF, 0xE1 };

/* Ideas from gstjpegparse.c */
GstTagList *
gst_droidcamsrc_exif_tags_from_jpeg_data (void *data, size_t size)
{
  GstByteReader reader;
  guint16 len = 0;
  const gchar *id;
  const guint8 *exif = NULL;
  GstBuffer *buff;
  GstTagList *tags = NULL;

  void *app1 = memmem (data, size, marker, 2);
  if (!app1) {
    GST_ERROR ("No tags found");
    goto out;
  }

  size -= (app1 - data);

  gst_byte_reader_init (&reader, app1, size);

  if (!gst_byte_reader_skip (&reader, 2)) {
    GST_ERROR ("Not enough jpeg data for tags");
    goto out;
  }

  if (!gst_byte_reader_get_uint16_be (&reader, &len)) {
    GST_ERROR ("Failed to get APP1 size");
    goto out;
  }

  len -= 2;                     /* for the marker itself */

  if (!gst_byte_reader_peek_string_utf8 (&reader, &id)) {
    goto out;
  }

  if (!strncmp (id, "Exif", 4)) {
    /* id + NUL + padding */
    if (!gst_byte_reader_skip (&reader, 6)) {
      goto out;
    }

    len -= 6;

    if (!gst_byte_reader_get_data (&reader, len, &exif)) {
      goto out;
    }

    buff =
        gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) exif,
        len, 0, len, NULL, NULL);

    tags = gst_tag_list_from_exif_buffer_with_tiff_header (buff);
    gst_buffer_unref (buff);

    return tags;
  }

out:
  return NULL;
}
#endif

GstTagList *
gst_droidcamsrc_exif_tags_from_jpeg_data (void *data, size_t size)
{
  GstTagList *tags = NULL;
  ExifMem *mem = exif_mem_new (g_malloc0, g_realloc, g_free);
  ExifData *exif = exif_data_new_mem (mem);
  unsigned char *exif_data = NULL;
  void *_exif_data = NULL;
  unsigned int exif_data_size = 0;
  GstBuffer *buffer;
  ExifEntry *iso;
  int x, i;

  exif_data_load_data (exif, data, size);
  exif_data_set_data_type (exif, EXIF_DATA_TYPE_COMPRESSED);

  exif_data_save_data (exif, &exif_data, &exif_data_size);
  if (!exif_data_size) {
    goto out;
  }

  if (exif_data_size <= 6) {
    goto out;
  }

  /* dump the data. based on libexif code */
  for (x = 0; x < EXIF_IFD_COUNT; x++) {
    if (exif->ifd[x] && exif->ifd[x]->count) {
      for (i = 0; i < exif->ifd[x]->count; i++) {
        char val[1024];
        ExifEntry *e = exif->ifd[x]->entries[i];
        GST_LOG ("Exif IFD: %s. Tag 0x%x (%s) = %s", exif_ifd_get_name (x),
            e->tag, exif_tag_get_name_in_ifd (e->tag, exif_entry_get_ifd (e)),
            exif_entry_get_value (e, val, sizeof (val)));
      }
    }
  }

  _exif_data = exif_data;

  exif_data += 6;
  exif_data_size -= 6;

  buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      exif_data, exif_data_size, 0, exif_data_size, NULL, NULL);
  tags = gst_tag_list_from_exif_buffer_with_tiff_header (buffer);
  gst_buffer_unref (buffer);

  /* We don't want these tags */
  gst_tag_list_remove_tag (tags, GST_TAG_DEVICE_MANUFACTURER);
  gst_tag_list_remove_tag (tags, GST_TAG_DEVICE_MODEL);
  gst_tag_list_remove_tag (tags, GST_TAG_APPLICATION_NAME);
  gst_tag_list_remove_tag (tags, GST_TAG_DATE_TIME);

  /* we have a mess with ISO so we will just behave as N9 */
  iso = exif_content_get_entry (exif->ifd[EXIF_IFD_EXIF],
      EXIF_TAG_ISO_SPEED_RATINGS);

  if (iso) {
#ifdef __arm__
    guint16 val = exif_get_short (iso->data, EXIF_BYTE_ORDER_MOTOROLA);
#else
    guint16 val = exif_get_short (iso->data, EXIF_BYTE_ORDER_INTEL);
#endif
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_CAPTURING_ISO_SPEED, val, NULL);
  }

  /* TODO: the following are being dropped
   *
   * 0x213  EXIF_TAG_YCBCR_POSITIONING
   * 0x9004 EXIF_TAG_DATE_TIME_DIGITIZED
   * 0x9101 EXIF_TAG_COMPONENTS_CONFIGURATION
   * 0xa001 EXIF_TAG_COLOR_SPACE
   * 0xa002 EXIF_TAG_PIXEL_X_DIMENSION
   * 0xa003 EXIF_TAG_PIXEL_Y_DIMENSION
   * 0xa005 EXIF_TAG_INTEROPERABILITY_IFD_POINTER
   * thumbnail.
   * 0x100 EXIF_TAG_IMAGE_WIDTH
   * 0x101 EXIF_TAG_IMAGE_LENGTH
   * 0x9203 EXIF_TAG_BRIGHTNESS_VALUE
   * 0x9205 EXIF_TAG_MAX_APERTURE_VALUE
   * 0x9206 EXIF_TAG_SUBJECT_DISTANCE
   * 0x9208 EXIF_TAG_LIGHT_SOURCE
   * 0x9286 EXIF_TAG_USER_COMMENT
   */
out:
  if (_exif_data) {
    exif_mem_free (mem, _exif_data);
  }

  if (exif) {
    exif_data_free (exif);
  }

  exif_mem_unref (mem);

  return tags;
}

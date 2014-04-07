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

#include "videoplayer.h"
#include <QQmlInfo>
#include <QTimer>
#include "renderer.h"
#include <QPainter>
#include <QMatrix4x4>
#include <cmath>

VideoPlayer::VideoPlayer(QQuickItem *parent) :
  QQuickPaintedItem(parent),
  m_renderer(0),
  m_bin(0),
  m_src(0),
  m_sink(0) {

  setRenderTarget(QQuickPaintedItem::FramebufferObject);
  setSmooth(false);
  setAntialiasing(false);
}

VideoPlayer::~VideoPlayer() {
  stop();

  if (m_bin) {
    gst_object_unref(m_bin);
    m_bin = 0;
  }
}

void VideoPlayer::componentComplete() {
  QQuickPaintedItem::componentComplete();
}

void VideoPlayer::classBegin() {
  QQuickPaintedItem::classBegin();

  m_bin = gst_pipeline_new (NULL);
  m_src = gst_element_factory_make("droidcamsrc", NULL);
  gst_bin_add (GST_BIN (m_bin), m_src);

  GstBus *bus = gst_element_get_bus(m_bin);
  gst_bus_add_watch(bus, bus_call, this);
  gst_object_unref(bus);
}

bool VideoPlayer::start() {
  if (!m_renderer) {
    m_renderer = QtCamViewfinderRenderer::create(this);
    if (!m_renderer) {
      qmlInfo(this) << "Failed to create viewfinder renderer";
      return false;
    }

    QObject::connect(m_renderer, SIGNAL(updateRequested()), this, SLOT(updateRequested()));
  }

  m_renderer->resize(QSizeF(width(), height()));

  if (!m_bin) {
    qmlInfo(this) << "no playbin";
    return false;
  }

  if (!m_sink) {
    m_sink = m_renderer->sinkElement();
    gst_bin_add (GST_BIN (m_bin), m_sink);
    gst_element_link_pads (m_src, "vfsrc", m_sink, "sink");
  } else {
    // This will allow resetting EGLDisplay
    m_renderer->sinkElement();
  }

  if (gst_element_set_state(m_bin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    qmlInfo(this) << "error setting pipeline to PLAYING";
    return false;
  }

  return true;
}

bool VideoPlayer::stop() {
  if (gst_element_set_state(m_bin, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
    qmlInfo(this) << "error setting pipeline to NULL";
    return false;
  }

  m_renderer->reset();

  return true;
}

void VideoPlayer::geometryChanged(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickPaintedItem::geometryChanged(newGeometry, oldGeometry);

  if (m_renderer) {
    m_renderer->resize(newGeometry.size());
  }
}


void VideoPlayer::paint(QPainter *painter) {
  painter->fillRect(contentsBoundingRect(), Qt::black);

  if (!m_renderer) {
    return;
  }

  bool needsNativePainting = m_renderer->needsNativePainting();

  if (needsNativePainting) {
    painter->beginNativePainting();
  }

  m_renderer->paint(QMatrix4x4(painter->combinedTransform()), painter->viewport());

  if (needsNativePainting) {
    painter->endNativePainting();
  }
}

gboolean VideoPlayer::bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  Q_UNUSED(bus);

  VideoPlayer *that = (VideoPlayer *) data;

  gchar *debug = NULL;
  GError *err = NULL;

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS:
    that->stop();
    break;

  case GST_MESSAGE_ERROR:
    gst_message_parse_error (msg, &err, &debug);
    qWarning() << "Error" << err->message;

    emit that->error(err->message, err->code, debug);
    that->stop();

    if (err) {
      g_error_free (err);
    }

    if (debug) {
      g_free (debug);
    }

    break;

  default:
    break;
  }

  return TRUE;
}

void VideoPlayer::updateRequested() {
  update();
}

// -*- c++ -*-

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

#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <QQuickPaintedItem>
#include <gst/gst.h>

class QtCamViewfinderRenderer;

class VideoPlayer : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY runningChanged);
  Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged);
  Q_PROPERTY(int device READ device WRITE setDevice NOTIFY deviceChanged);

public:
  VideoPlayer(QQuickItem *parent = 0);
  ~VideoPlayer();

  bool running();

  int mode();
  void setMode(int mode);

  int device();
  void setDevice(int device);

  virtual void componentComplete();
  virtual void classBegin();

  void paint(QPainter *painter);

  Q_INVOKABLE bool start();
  Q_INVOKABLE bool stop();

public slots:
  void capture();

signals:
  void error(const QString& message, int code, const QString& debug);
  void runningChanged();
  void modeChanged();
  void deviceChanged();

protected:
  void geometryChanged(const QRectF& newGeometry, const QRectF& oldGeometry);

private slots:
  void updateRequested();

private:
  static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);

  QtCamViewfinderRenderer *m_renderer;

  GstElement *m_bin, *m_src, *m_sink;
  bool m_created;
};

#endif /* VIDEO_PLAYER_H */

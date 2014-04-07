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

#include <QGuiApplication>
#include <QDebug>
#include <gst/gst.h>
#include <QGuiApplication>
#include <QQuickView>
#include <QQmlError>
#include <QQmlEngine>
#include <QUrl>
#include <QQmlContext>
#include "videoplayer.h"

int
main(int argc, char *argv[]) {
  gst_init (&argc, &argv);

  QGuiApplication app(argc, argv);
  QQuickView view;

  view.setResizeMode(QQuickView::SizeRootObjectToView);
  view.setPersistentOpenGLContext(true);
  view.setPersistentSceneGraph(true);

  qmlRegisterType<VideoPlayer>("VideoPlayer", 1, 0, "VideoPlayer");

  view.setSource(QUrl("qrc:/main.qml"));

  if (view.status() == QQuickView::Error) {
    qCritical() << "Errors loading QML:";
    QList<QQmlError> errors = view.errors();

    foreach (const QQmlError& error, errors) {
      qCritical() << error.toString();
    }

    return 1;
  }

  view.showFullScreen();

  return app.exec();
}

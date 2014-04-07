TEMPLATE = app
TARGET = simple-camera
INCLUDEPATH += .
QT += gui qml quick opengl

CONFIG += link_pkgconfig
PKGCONFIG += gstreamer-1.0 nemo-gstreamer-interfaces-1.0 Qt5OpenGLExtensions egl gstreamer-video-1.0

SOURCES += main.cpp videoplayer.cpp renderer.cpp renderernemo.cpp
HEADERS += videoplayer.h renderer.h renderernemo.h
RESOURCES += qml.qrc

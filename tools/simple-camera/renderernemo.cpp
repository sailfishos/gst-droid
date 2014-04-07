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

#include "renderernemo.h"
#include <QDebug>
#include <gst/video/video.h>
#include <QGLShaderProgram>
#include <gst/interfaces/nemovideotexture.h>
#include <EGL/egl.h>
#include <QOpenGLContext>
#include <QOpenGLExtensions>

typedef void *EGLSyncKHR;
#define EGL_SYNC_FENCE_KHR                       0x30F9

typedef EGLSyncKHR(EGLAPIENTRYP PFNEGLCREATESYNCKHRPROC)(EGLDisplay dpy, EGLenum type,
							  const EGLint *attrib_list);

PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR = 0;

static const QString FRAGMENT_SHADER = ""
    "#extension GL_OES_EGL_image_external: enable\n"
    "uniform samplerExternalOES texture0;"
    "varying lowp vec2 fragTexCoord;"
    "void main() {"
    "  gl_FragColor = texture2D(texture0, fragTexCoord);"
    "}"
  "";

static const QString VERTEX_SHADER = ""
    "attribute highp vec4 inputVertex;"
    "attribute lowp vec2 textureCoord;"
    "uniform highp mat4 matrix;"
    "uniform highp mat4 matrixWorld;"
    "varying lowp vec2 fragTexCoord;"
    ""
    "void main() {"
    "  gl_Position = matrix * matrixWorld * inputVertex;"
    "  fragTexCoord = textureCoord;"
    "}"
  "";

QtCamViewfinderRendererNemo::QtCamViewfinderRendererNemo(QObject *parent) :
  QtCamViewfinderRenderer(parent),
  m_sink(0),
  m_frame(-1),
  m_id(0),
  m_notify(0),
  m_needsInit(true),
  m_program(0),
  m_displaySet(false),
  m_img(0) {

  m_texCoords.resize(8);
  m_vertexCoords.resize(8);

  m_texCoords[0] = 0;       m_texCoords[1] = 0;
  m_texCoords[2] = 1;       m_texCoords[3] = 0;
  m_texCoords[4] = 1;       m_texCoords[5] = 1;
  m_texCoords[6] = 0;       m_texCoords[7] = 1;

  for (int x = 0; x < 8; x++) {
    m_vertexCoords[x] = 0;
  }
}

QtCamViewfinderRendererNemo::~QtCamViewfinderRendererNemo() {
  cleanup();

  if (m_program) {
    delete m_program;
    m_program = 0;
  }

  if (m_img) {
    delete m_img;
    m_img = 0;
  }
}

bool QtCamViewfinderRendererNemo::needsNativePainting() {
  return true;
}

void QtCamViewfinderRendererNemo::paint(const QMatrix4x4& matrix, const QRectF& viewport) {
  if (!m_img) {
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
      qCritical() << "No current OpenGL context";
      return;
    }

    if (!ctx->hasExtension("GL_OES_EGL_image")) {
      qCritical() << "GL_OES_EGL_image not supported";
      return;
    }

    m_img = new QOpenGLExtension_OES_EGL_image;

    if (!m_img->initializeOpenGLFunctions()) {
      qCritical() << "Failed to initialize GL_OES_EGL_image";
      delete m_img;
      m_img = 0;
      return;
    }
  }

  if (m_dpy == EGL_NO_DISPLAY) {
    m_dpy = eglGetCurrentDisplay();
  }

  if (m_dpy == EGL_NO_DISPLAY) {
    qCritical() << "Failed to obtain EGL Display";
    //    return;
  }

  if (m_sink && m_dpy != EGL_NO_DISPLAY && !m_displaySet) {
    g_object_set(G_OBJECT(m_sink), "egl-display", m_dpy, NULL);
    m_displaySet = true;
  }

  QMutexLocker locker(&m_frameMutex);
  if (m_frame == -1) {
    return;
  }

  if (m_needsInit) {
    calculateProjectionMatrix(viewport);

    if (!eglCreateSyncKHR) {
      eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");

      if (!eglCreateSyncKHR) {
	qWarning() << "eglCreateSyncKHR not found. Fences disabled";
      }
    }

    m_needsInit = false;
  }

  if (!m_program) {
    // Program will be created if needed and will never be deleted even
    // if attaching the shaders fail.
    createProgram();
  }

  paintFrame(matrix, m_frame);
}

void QtCamViewfinderRendererNemo::resize(const QSizeF& size) {
  if (size == m_size) {
    return;
  }

  m_size = size;

  m_renderArea = QRectF();

  calculateVertexCoords();

  // This will destroy everything
  // but we need a way to reset the viewport and the transformation matrix only.
  m_needsInit = true;

  emit renderAreaChanged();
}

void QtCamViewfinderRendererNemo::reset() {
  QMutexLocker locker(&m_frameMutex);
  m_frame = -1;
  m_displaySet = false;
  // TODO: more? delete m_progrem, m_img and set m_needsInit?
}

GstElement *QtCamViewfinderRendererNemo::sinkElement() {
  if (!m_sink) {
    m_sink = gst_element_factory_make("droideglsink",
				      "QtCamViewfinderRendererNemoSink");
    if (!m_sink) {
      qCritical() << "Failed to create droideglsink";
      return 0;
    }

    g_object_add_toggle_ref(G_OBJECT(m_sink), (GToggleNotify)sink_notify, this);
    m_displaySet = false;
  }

  m_dpy = eglGetCurrentDisplay();
  if (m_dpy == EGL_NO_DISPLAY) {
    qCritical() << "Failed to obtain EGL Display";
  }
  else {
    g_object_set(G_OBJECT(m_sink), "egl-display", m_dpy, NULL);
    m_displaySet = true;
  }

  m_id = g_signal_connect(G_OBJECT(m_sink), "frame-ready", G_CALLBACK(frame_ready), this);

  GstPad *pad = gst_element_get_static_pad(m_sink, "sink");
  m_notify = g_signal_connect(G_OBJECT(pad), "notify::caps",
			      G_CALLBACK(sink_caps_changed), this);
  gst_object_unref(pad);

  return m_sink;
}

void QtCamViewfinderRendererNemo::frame_ready(GstElement *sink, int frame,
					       QtCamViewfinderRendererNemo *r) {
  Q_UNUSED(sink);
  Q_UNUSED(frame);

  r->m_frameMutex.lock();
  r->m_frame = frame;
  r->m_frameMutex.unlock();

  QMetaObject::invokeMethod(r, "updateRequested", Qt::QueuedConnection);
}

void QtCamViewfinderRendererNemo::sink_notify(QtCamViewfinderRendererNemo *q,
					       GObject *object, gboolean is_last_ref) {

  Q_UNUSED(object);

  if (is_last_ref) {
    q->cleanup();
  }
}

void QtCamViewfinderRendererNemo::sink_caps_changed(GObject *obj, GParamSpec *pspec,
						     QtCamViewfinderRendererNemo *q) {
  Q_UNUSED(pspec);

  if (!obj) {
    return;
  }

  if (!GST_IS_PAD (obj)) {
    return;
  }

  GstPad *pad = GST_PAD (obj);
  GstCaps *caps = gst_pad_get_current_caps (pad);
  if (!caps) {
    return;
  }

  if (gst_caps_get_size (caps) < 1) {
    gst_caps_unref (caps);
    return;
  }

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps)) {
    qWarning() << "failed to get video info";
    gst_caps_unref (caps);
    return;
  }

  QMetaObject::invokeMethod(q, "setVideoSize", Qt::QueuedConnection,
			    Q_ARG(QSizeF, QSizeF(info.width, info.height)));

  gst_caps_unref (caps);
}

void QtCamViewfinderRendererNemo::calculateProjectionMatrix(const QRectF& rect) {
  m_projectionMatrix = QMatrix4x4();
  m_projectionMatrix.ortho(rect);
}

void QtCamViewfinderRendererNemo::createProgram() {
  if (m_program) {
    delete m_program;
  }

  m_program = new QGLShaderProgram;

  if (!m_program->addShaderFromSourceCode(QGLShader::Vertex, VERTEX_SHADER)) {
    qCritical() << "Failed to add vertex shader";
    return;
  }

  if (!m_program->addShaderFromSourceCode(QGLShader::Fragment, FRAGMENT_SHADER)) {
    qCritical() << "Failed to add fragment shader";
    return;
  }

  m_program->bindAttributeLocation("inputVertex", 0);
  m_program->bindAttributeLocation("textureCoord", 1);

  if (!m_program->link()) {
    qCritical() << "Failed to link program!";
    return;
  }

  if (!m_program->bind()) {
    qCritical() << "Failed to bind program";
    return;
  }

  m_program->setUniformValue("texture0", 0); // texture UNIT 0
  m_program->release();
}

void QtCamViewfinderRendererNemo::paintFrame(const QMatrix4x4& matrix, int frame) {
  EGLSyncKHR sync = 0;
  EGLImageKHR img = 0;

  if (frame == -1) {
    return;
  }

  NemoGstVideoTexture *sink = NEMO_GST_VIDEO_TEXTURE(m_sink);

  if (!nemo_gst_video_texture_acquire_frame(sink)) {
    qDebug() << "Failed to acquire frame";
    return;
  }

  std::vector<GLfloat> texCoords(m_texCoords);
  /*
  // Now take into account cropping:
  const GstStructure *s =
    nemo_gst_video_texture_get_frame_qdata (sink,
					    g_quark_from_string ("GstDroidCamSrcCropData"));
  if (s) {
    updateCropInfo(s, texCoords);
  }
  */
  if (!nemo_gst_video_texture_bind_frame(sink, &img)) {
    qDebug() << "Failed to bind frame";
    nemo_gst_video_texture_release_frame(sink, NULL);
    return;
  }

  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture(GL_TEXTURE0);

  m_program->bind();

  m_img->glEGLImageTargetTexture2DOES (GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);

  m_program->setUniformValue("matrix", m_projectionMatrix);
  m_program->setUniformValue("matrixWorld", matrix);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, &m_vertexCoords[0]);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, &texCoords[0]);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(0);

  m_program->release();

  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

  nemo_gst_video_texture_unbind_frame(sink);

  if (eglCreateSyncKHR) {
    sync = eglCreateSyncKHR(m_dpy, EGL_SYNC_FENCE_KHR, NULL);
  }

  nemo_gst_video_texture_release_frame(sink, sync);
  glDeleteTextures (1, &texture);
}

void QtCamViewfinderRendererNemo::calculateVertexCoords() {
  if (!m_size.isValid() || !m_videoSize.isValid()) {
    return;
  }

  QRectF area = renderArea();

  qreal leftMargin = area.x();
  qreal topMargin = area.y();
  QSizeF renderSize = area.size();

  m_vertexCoords[0] = leftMargin;
  m_vertexCoords[1] = topMargin + renderSize.height();

  m_vertexCoords[2] = renderSize.width() + leftMargin;
  m_vertexCoords[3] = topMargin + renderSize.height();

  m_vertexCoords[4] = renderSize.width() + leftMargin;
  m_vertexCoords[5] = topMargin;

  m_vertexCoords[6] = leftMargin;
  m_vertexCoords[7] = topMargin;
}

QRectF QtCamViewfinderRendererNemo::renderArea() {
  if (!m_renderArea.isNull()) {
    return m_renderArea;
  }

  QSizeF renderSize = m_videoSize;
  renderSize.scale(m_size, Qt::KeepAspectRatio);

  qreal leftMargin = (m_size.width() - renderSize.width())/2.0;
  qreal topMargin = (m_size.height() - renderSize.height())/2.0;

  m_renderArea = QRectF(QPointF(leftMargin, topMargin), renderSize);

  return m_renderArea;
}

QSizeF QtCamViewfinderRendererNemo::videoResolution() {
  return m_videoSize;
}

void QtCamViewfinderRendererNemo::setVideoSize(const QSizeF& size) {
  if (size == m_videoSize) {
    return;
  }

  m_videoSize = size;

  m_renderArea = QRectF();

  calculateVertexCoords();

  m_needsInit = true;

  emit renderAreaChanged();
  emit videoResolutionChanged();
}

void QtCamViewfinderRendererNemo::cleanup() {
  if (!m_sink) {
    return;
  }

  if (m_id) {
    g_signal_handler_disconnect(m_sink, m_id);
    m_id = 0;
  }

  if (m_notify) {
    g_signal_handler_disconnect(m_sink, m_notify);
    m_notify = 0;
  }

  g_object_remove_toggle_ref(G_OBJECT(m_sink), (GToggleNotify)sink_notify, this);
  m_sink = 0;
}

void QtCamViewfinderRendererNemo::updateCropInfo(const GstStructure *s,
						 std::vector<GLfloat>& texCoords) {
  int right = 0, bottom = 0, top = 0, left = 0;

  if (!gst_structure_get_int(s, "top", &top) ||
      !gst_structure_get_int(s, "left", &left) ||
      !gst_structure_get_int(s, "bottom", &bottom) ||
      !gst_structure_get_int(s, "right", &right)) {
    qWarning() << "incomplete crop info";
    return;
  }

  if ((right - left) <= 0 || (bottom - top) <= 0) {
    // empty rect.
    return;
  }

  int width = right - left;
  int height = bottom - top;
  qreal tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f;
  int bufferWidth = m_videoSize.width();
  int bufferHeight = m_videoSize.height();
  if (width < bufferWidth) {
    tx = (qreal)left / (qreal)bufferWidth;
    sx = (qreal)right / (qreal)bufferWidth;
  }

  if (height < bufferHeight) {
    ty = (qreal)top / (qreal)bufferHeight;
    sy = (qreal)bottom / (qreal)bufferHeight;
  }

  texCoords[0] = tx;       texCoords[1] = ty;
  texCoords[2] = sx;       texCoords[3] = ty;
  texCoords[4] = sx;       texCoords[5] = sy;
  texCoords[6] = tx;       texCoords[7] = sy;

  //  qWarning() << top << left << bottom << right << tx << sx << ty << sy;
}

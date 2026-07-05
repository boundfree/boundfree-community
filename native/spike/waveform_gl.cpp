#include "waveform_gl.h"

#include <cstddef>

static const char* kVert = R"(
attribute float aX;
attribute float aCh;
attribute float aUv;
uniform int uNCh;
uniform float uGain;
void main() {
    float laneH = 2.0 / float(uNCh);
    float yCenter = 1.0 - laneH * (aCh + 0.5);
    float y = yCenter + aUv * uGain;
    float x = aX * 2.0 - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

static const char* kFrag = R"(
void main() {
    gl_FragColor = vec4(0.41, 0.14, 0.48, 1.0);
}
)";

WaveformGL::WaveformGL(QWidget* parent) : QOpenGLWidget(parent) {}

void WaveformGL::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    prog_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVert);
    prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag);
    prog_.link();
    vbo_.create();
}

void WaveformGL::setData(int nCh, int nSamp, const float* uv) {
    nCh_ = nCh;
    nSamp_ = nSamp;
    verts_.resize(static_cast<std::size_t>(nCh) * static_cast<std::size_t>(nSamp) * 3);

    for (int c = 0; c < nCh; ++c) {
        for (int s = 0; s < nSamp; ++s) {
            const std::size_t src = static_cast<std::size_t>(c) * static_cast<std::size_t>(nSamp) +
                                    static_cast<std::size_t>(s);
            const std::size_t dst = src * 3;
            verts_[dst + 0] = static_cast<float>(s) / static_cast<float>(nSamp - 1);
            verts_[dst + 1] = static_cast<float>(c);
            verts_[dst + 2] = uv[src];
        }
    }

    dirty_ = true;
    update();
}

void WaveformGL::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
    if (nCh_ == 0 || nSamp_ == 0 || !prog_.isLinked()) {
        emit frameRendered();
        return;
    }

    prog_.bind();
    vbo_.bind();

    if (dirty_) {
        vbo_.allocate(verts_.data(), static_cast<int>(verts_.size() * sizeof(float)));
        dirty_ = false;
    }

    const int stride = 3 * static_cast<int>(sizeof(float));
    prog_.enableAttributeArray("aX");
    prog_.setAttributeBuffer("aX", GL_FLOAT, 0, 1, stride);
    prog_.enableAttributeArray("aCh");
    prog_.setAttributeBuffer("aCh", GL_FLOAT, static_cast<int>(sizeof(float)), 1, stride);
    prog_.enableAttributeArray("aUv");
    prog_.setAttributeBuffer("aUv", GL_FLOAT, 2 * static_cast<int>(sizeof(float)), 1, stride);
    prog_.setUniformValue("uNCh", nCh_);
    prog_.setUniformValue("uGain", 0.0008f);

    for (int c = 0; c < nCh_; ++c) {
        glDrawArrays(GL_LINE_STRIP, c * nSamp_, nSamp_);
    }

    vbo_.release();
    prog_.release();
    emit frameRendered();
}

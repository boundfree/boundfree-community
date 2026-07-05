#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <vector>

class WaveformGL : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit WaveformGL(QWidget* parent = nullptr);

    void setData(int nCh, int nSamp, const float* uv);

signals:
    void frameRendered();

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    int nCh_ = 0;
    int nSamp_ = 0;
    std::vector<float> verts_;
    QOpenGLShaderProgram prog_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    bool dirty_ = false;
};

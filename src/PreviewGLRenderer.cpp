#include "PreviewGLRenderer.h"

#include <QImage>
#include <QtMath>
#include <QtGlobal>

#include <cstddef>

namespace {
struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};
}

void PreviewGLRenderer::initialize()
{
    if (initialized_) {
        return;
    }

    initializeOpenGLFunctions();
    initialized_ = true;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void PreviewGLRenderer::shutdown()
{
    for (auto it = textureCache_.begin(); it != textureCache_.end(); ++it) {
        if (it.value() != 0) {
            GLuint texture = it.value();
            glDeleteTextures(1, &texture);
        }
    }
    textureCache_.clear();
    if (vertexBuffer_ != 0) {
        glDeleteBuffers(1, &vertexBuffer_);
        vertexBuffer_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vertexShader_ != 0) {
        glDeleteShader(vertexShader_);
        vertexShader_ = 0;
    }
    if (fragmentShader_ != 0) {
        glDeleteShader(fragmentShader_);
        fragmentShader_ = 0;
    }

    viewportSize_ = QSize();
    devicePixelRatio_ = 1.0;
    positionLocation_ = -1;
    uvLocation_ = -1;
    textureLocation_ = -1;
    opacityLocation_ = -1;
    initialized_ = false;
}

void PreviewGLRenderer::beginFrame(const QSize& viewportSize, qreal devicePixelRatio)
{
    if (!initialized_) {
        return;
    }

    viewportSize_ = viewportSize;
    devicePixelRatio_ = qMax<qreal>(1.0, devicePixelRatio);

    glViewport(
        0,
        0,
        qMax(1, qRound(viewportSize.width() * devicePixelRatio_)),
        qMax(1, qRound(viewportSize.height() * devicePixelRatio_))
    );
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

bool PreviewGLRenderer::drawImageQuad(
    const QImage& image,
    const QRectF& targetRect,
    qreal angleDegrees,
    qreal opacity,
    const QRectF& sourceRect
)
{
    if (opacity <= 0.0) {
        return true;
    }
    if (!initialized_ || image.isNull() || targetRect.isEmpty() || viewportSize_.isEmpty()) {
        return false;
    }
    if (!ensureProgram()) {
        return false;
    }
    const GLuint texture = ensureTexture(image);
    if (texture == 0) {
        return false;
    }

    const float width = static_cast<float>(qMax(1, viewportSize_.width()));
    const float height = static_cast<float>(qMax(1, viewportSize_.height()));
    const QRectF uvSource = sourceRect.isValid() && !sourceRect.isEmpty()
        ? sourceRect.intersected(QRectF(0.0, 0.0, image.width(), image.height()))
        : QRectF(0.0, 0.0, image.width(), image.height());
    const float u0 = static_cast<float>(uvSource.left() / qMax(1, image.width()));
    const float v0 = static_cast<float>(uvSource.top() / qMax(1, image.height()));
    const float u1 = static_cast<float>((uvSource.left() + uvSource.width()) / qMax(1, image.width()));
    const float v1 = static_cast<float>((uvSource.top() + uvSource.height()) / qMax(1, image.height()));
    const QPointF center = targetRect.center();
    const qreal radians = qDegreesToRadians(angleDegrees);
    const qreal sinAngle = qSin(radians);
    const qreal cosAngle = qCos(radians);
    const QPointF localCorners[] = {
        QPointF(-targetRect.width() / 2.0, -targetRect.height() / 2.0),
        QPointF(-targetRect.width() / 2.0, targetRect.height() / 2.0),
        QPointF(targetRect.width() / 2.0, -targetRect.height() / 2.0),
        QPointF(targetRect.width() / 2.0, targetRect.height() / 2.0),
    };
    QPointF rotatedCorners[4];
    for (int i = 0; i < 4; ++i) {
        const QPointF& local = localCorners[i];
        rotatedCorners[i] = QPointF(
            center.x() + local.x() * cosAngle - local.y() * sinAngle,
            center.y() + local.x() * sinAngle + local.y() * cosAngle
        );
    }

    const auto toNdc = [width, height](const QPointF& point) -> QPointF {
        return QPointF(
            (point.x() / width) * 2.0 - 1.0,
            1.0 - (point.y() / height) * 2.0
        );
    };
    const QPointF topLeft = toNdc(rotatedCorners[0]);
    const QPointF bottomLeft = toNdc(rotatedCorners[1]);
    const QPointF topRight = toNdc(rotatedCorners[2]);
    const QPointF bottomRight = toNdc(rotatedCorners[3]);
    const QuadVertex vertices[] = {
        {static_cast<float>(topLeft.x()), static_cast<float>(topLeft.y()), u0, v0},
        {static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), u0, v1},
        {static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), u1, v0},
        {static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), u1, v0},
        {static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), u0, v1},
        {static_cast<float>(bottomRight.x()), static_cast<float>(bottomRight.y()), u1, v1},
    };

    if (positionLocation_ < 0 || uvLocation_ < 0 || textureLocation_ < 0) {
        return false;
    }

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(textureLocation_, 0);
    if (opacityLocation_ >= 0) {
        glUniform1f(opacityLocation_, static_cast<GLfloat>(qBound<qreal>(0.0, opacity, 1.0)));
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(static_cast<GLuint>(positionLocation_));
    glVertexAttribPointer(
        static_cast<GLuint>(positionLocation_),
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(QuadVertex),
        reinterpret_cast<const void*>(offsetof(QuadVertex, x))
    );
    glEnableVertexAttribArray(static_cast<GLuint>(uvLocation_));
    glVertexAttribPointer(
        static_cast<GLuint>(uvLocation_),
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(QuadVertex),
        reinterpret_cast<const void*>(offsetof(QuadVertex, u))
    );
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(static_cast<GLuint>(positionLocation_));
    glDisableVertexAttribArray(static_cast<GLuint>(uvLocation_));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);

    return true;
}

bool PreviewGLRenderer::isInitialized() const
{
    return initialized_;
}

QSize PreviewGLRenderer::viewportSize() const
{
    return viewportSize_;
}

qreal PreviewGLRenderer::devicePixelRatio() const
{
    return devicePixelRatio_;
}

bool PreviewGLRenderer::ensureProgram()
{
    if (program_ != 0) {
        return true;
    }

    static const char* kVertexSource =
        "attribute vec2 a_position;\n"
        "attribute vec2 a_texCoord;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
        "    v_texCoord = a_texCoord;\n"
        "}\n";
    static const char* kFragmentSource =
        "uniform sampler2D u_texture;\n"
        "uniform float u_opacity;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(u_texture, v_texCoord) * u_opacity;\n"
        "}\n";

    vertexShader_ = glCreateShader(GL_VERTEX_SHADER);
    fragmentShader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (vertexShader_ == 0 || fragmentShader_ == 0) {
        return false;
    }

    glShaderSource(vertexShader_, 1, &kVertexSource, nullptr);
    glCompileShader(vertexShader_);
    GLint compileStatus = 0;
    glGetShaderiv(vertexShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }

    glShaderSource(fragmentShader_, 1, &kFragmentSource, nullptr);
    glCompileShader(fragmentShader_);
    glGetShaderiv(fragmentShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }

    program_ = glCreateProgram();
    if (program_ == 0) {
        return false;
    }

    glAttachShader(program_, vertexShader_);
    glAttachShader(program_, fragmentShader_);
    glLinkProgram(program_);
    GLint linkStatus = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        return false;
    }

    positionLocation_ = glGetAttribLocation(program_, "a_position");
    uvLocation_ = glGetAttribLocation(program_, "a_texCoord");
    textureLocation_ = glGetUniformLocation(program_, "u_texture");
    opacityLocation_ = glGetUniformLocation(program_, "u_opacity");
    if (positionLocation_ < 0 || uvLocation_ < 0 || textureLocation_ < 0) {
        return false;
    }

    glGenBuffers(1, &vertexBuffer_);
    return vertexBuffer_ != 0;
}

GLuint PreviewGLRenderer::ensureTexture(const QImage& image)
{
    const quint64 cacheKey = image.cacheKey();
    const auto cached = textureCache_.constFind(cacheKey);
    if (cached != textureCache_.cend() && cached.value() != 0) {
        return cached.value();
    }

    const QImage textureImage = image.convertToFormat(QImage::Format_RGBA8888);
    if (textureImage.isNull()) {
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        textureImage.width(),
        textureImage.height(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        textureImage.constBits()
    );
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    textureCache_.insert(cacheKey, texture);
    return texture;
}

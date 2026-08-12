#include "emission_antenna.h"

#include <QMetaObject>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimerEvent>
#include <QtMath>

namespace {

constexpr const char *kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 a_position;
layout (location = 1) in vec2 a_uv;

out vec2 v_uv;

void main()
{
    v_uv = a_uv;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

constexpr const char *kFragmentShaderSource = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;

uniform vec2 u_resolutionPx;                 // framebuffer size in physical pixels
uniform float u_dpr;                         // device pixel ratio
uniform float u_nowSec;                      // global time in seconds
uniform float u_waveBirthSec[16];            // ring buffer (birth time in seconds), < 0 => inactive
uniform float u_waveSpeedPxPerSec;           // logical px/s
uniform float u_waveMaxRadiusPx;             // logical px
uniform float u_waveThicknessPx;             // logical px

float sdSegment(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-5), 0.0, 1.0);
    return length(pa - ba * h);
}

float lineMask(vec2 p, vec2 a, vec2 b, float halfWidthPx, float aaPx)
{
    float d = sdSegment(p, a, b);
    return 1.0 - smoothstep(halfWidthPx - aaPx, halfWidthPx + aaPx, d);
}

// Залитый круг (антиалиас по краю).
float filledDiskMask(vec2 p, vec2 c, float r, float aaPx)
{
    float dist = length(p - c);
    return 1.0 - smoothstep(r - aaPx, r + aaPx, dist);
}

void main()
{
    // Pixel-space с началом координат в нижнем левом углу:
    // X -> вправо, Y -> вверх.
    vec2 p = vec2(v_uv.x * u_resolutionPx.x, v_uv.y * u_resolutionPx.y);

    vec3 background = vec3(0.074510, 0.121569, 0.227451); // #131f3a (как framePowerSettings)
    vec3 color = background;

    float aaPx = max(1.0, 0.75 * u_dpr);
    float lineHalfWidthPx = 1.2 * u_dpr;                  // 2.4px total on DPR=1
    float txRadiusPx = 2.9 * u_dpr;

    // Передатчик на ножке. Центр круга заметно выше середины виджета: так кончик излучателя
    // визуально отделён от «хорды» первых полукругов (окружности центрированы в вершине дуги,
    // её нижняя кромка даёт горизонтальный отрезок — поднятый диск читается как источник).
    vec2 txCenter = vec2(u_resolutionPx.x * 0.5, u_resolutionPx.y * 0.4); // 0.5 - центр излучателя
    float diskBottomY = txCenter.y - txRadiusPx;
    vec2 stalkBottom = vec2(txCenter.x, u_resolutionPx.y * 0.25); // 0.3 - ножка
    vec2 stalkTop = vec2(txCenter.x, diskBottomY);

    float stalkMask = lineMask(p, stalkBottom, stalkTop, lineHalfWidthPx, aaPx);
    float diskMask = filledDiskMask(p, txCenter, txRadiusPx, aaPx);
    float antennaMask = max(stalkMask, diskMask);

    color = mix(color, vec3(1.0), clamp(antennaMask, 0.0, 1.0));

    // Полукруговые радиоволны: от верхней точки залитого круга, в сторону +Y.
    vec2 waveOrigin = vec2(txCenter.x, txCenter.y + txRadiusPx);
    float waveSpeedPxPerSec = u_waveSpeedPxPerSec * u_dpr;
    // Дальше на малых виджетах — увеличенная доля кадра (см. kWaveMaxRadiusPx в C++).
    float waveMaxRadiusPx = min(u_waveMaxRadiusPx * u_dpr, 0.55 * min(u_resolutionPx.x, u_resolutionPx.y)); //0.55 - круги
    float waveHalfWidthPx = 0.5 * u_waveThicknessPx * u_dpr;

    vec3 waveAccum = vec3(0.0);

    for (int i = 0; i < 16; ++i) {
        float birth = u_waveBirthSec[i];
        if (birth < 0.0) {
            continue;
        }

        float age = u_nowSec - birth;
        if (age < 1e-4) {
            continue;
        }

        float radius = age * waveSpeedPxPerSec;
        if (radius > waveMaxRadiusPx) {
            continue;
        }

        float ringDistance = abs(length(p - waveOrigin) - radius);
        float ringMask = 1.0 - smoothstep(waveHalfWidthPx - aaPx, waveHalfWidthPx + aaPx, ringDistance);

        // Верхняя полуплоскость: над горизонталью через центр (ось Y вверх).
        float hemisphereMask = smoothstep(-aaPx, aaPx, p.y - waveOrigin.y);

        // Затухание: alpha ~ (1 - r/maxR) * 1/r (чуть медленнее по радиусу — ярче середины дуги).
        float linearFade = max(0.0, 1.0 - (radius / max(waveMaxRadiusPx, 1.0)));
        linearFade = pow(linearFade, 0.85);
        float invRadiusFade = 1.0 / (1.0 + 0.018 * radius);
        float alpha = ringMask * hemisphereMask * linearFade * invRadiusFade;

        float grad = clamp(radius / max(waveMaxRadiusPx, 1.0), 0.0, 1.0);
        // Более насыщенный cyan → глубокий синий; усиление общей интенсивности кольца.
        vec3 nearColor = vec3(0.0, 0.98, 1.0);
        vec3 farColor = vec3(0.0, 0.35, 1.0);
        vec3 waveColor = mix(nearColor, farColor, grad);

        const float kWaveBoost = 2.15;
        waveAccum += waveColor * alpha * kWaveBoost;
    }

    color += waveAccum;
    color = clamp(color, 0.0, 1.0);

    fragColor = vec4(color, 1.0);
}
)";

} // namespace

EmissionAntennaWidget::EmissionAntennaWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(fmt);

    setMinimumSize(110, 56);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    resetWaves();
}

EmissionAntennaWidget::~EmissionAntennaWidget()
{
    if (context()) {
        makeCurrent();
        cleanupGlResources();
        doneCurrent();
    }
}

void EmissionAntennaWidget::startTransmission()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &EmissionAntennaWidget::startTransmission, Qt::QueuedConnection);
        return;
    }

    if (!m_elapsedTimer.isValid()) {
        m_elapsedTimer.start();
    }

    m_transmitting = true;
    m_waveWriteIndex = 0;
    resetWaves();

    const float nowSec = static_cast<float>(m_elapsedTimer.elapsed()) * 0.001f;
    m_lastFrameSec = nowSec;
    m_nextWaveSpawnSec = nowSec;
    spawnWave(nowSec); // плавный старт: первая волна без задержки.
    m_nextWaveSpawnSec += kWaveSpawnIntervalSec;

    if (!m_frameTimer.isActive()) {
        m_frameTimer.start(kFrameIntervalMs, this);
    }

    update();
}

void EmissionAntennaWidget::stopTransmission()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &EmissionAntennaWidget::stopTransmission, Qt::QueuedConnection);
        return;
    }

    m_transmitting = false;
    resetWaves();
    m_frameTimer.stop();
    update();
}

void EmissionAntennaWidget::initializeGL()
{
    initializeOpenGLFunctions();
    if (context()) {
        const QSurfaceFormat fmt = context()->format();
        if (fmt.majorVersion() < 3 || (fmt.majorVersion() == 3 && fmt.minorVersion() < 3)) {
            qWarning() << "EmissionAntennaWidget requires OpenGL 3.3+," << "got"
                       << fmt.majorVersion() << "." << fmt.minorVersion();
        }
    }
    setupShaders();
    setupFullscreenQuad();
    ensureDebugLogger();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.074510f, 0.121569f, 0.227451f, 1.0f); // #131f3a (как framePowerSettings)

    if (!m_elapsedTimer.isValid()) {
        m_elapsedTimer.start();
    }
}

void EmissionAntennaWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w)
    Q_UNUSED(h)
    const float dpr = devicePixelRatioF();
    glViewport(0, 0, qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
}

void EmissionAntennaWidget::paintGL()
{
    const float dpr = static_cast<float>(devicePixelRatioF());
    const int fbWidth = qMax(1, qRound(width() * dpr));
    const int fbHeight = qMax(1, qRound(height() * dpr));
    glViewport(0, 0, fbWidth, fbHeight);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_elapsedTimer.isValid()) {
        m_elapsedTimer.start();
    }

    const float nowSec = static_cast<float>(m_elapsedTimer.elapsed()) * 0.001f;
    float dtSec = nowSec - m_lastFrameSec;
    if (dtSec < 0.0f) {
        dtSec = 0.0f;
    } else if (dtSec > 0.1f) {
        // Защита от резкого скачка dt после stop-the-world (debugger/suspend).
        dtSec = 0.1f;
    }
    m_lastFrameSec = nowSec;

    updateAnimationState(nowSec, dtSec);

    m_program.bind();
    m_program.setUniformValue("u_resolutionPx", QVector2D(static_cast<float>(fbWidth), static_cast<float>(fbHeight)));
    m_program.setUniformValue("u_dpr", dpr);
    m_program.setUniformValue("u_nowSec", nowSec);
    m_program.setUniformValue("u_waveSpeedPxPerSec", kWaveSpeedPxPerSec);
    m_program.setUniformValue("u_waveMaxRadiusPx", kWaveMaxRadiusPx);
    m_program.setUniformValue("u_waveThicknessPx", kWaveThicknessPx);

    GLfloat waveBirthSec[kMaxWaves];
    for (int i = 0; i < kMaxWaves; ++i) {
        waveBirthSec[i] = m_waves[static_cast<std::size_t>(i)].birthTimeSec;
    }
    m_program.setUniformValueArray("u_waveBirthSec", waveBirthSec, kMaxWaves, 1);

    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();
    m_program.release();
}

void EmissionAntennaWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_frameTimer.timerId()) {
        update();
        return;
    }
    QOpenGLWidget::timerEvent(event);
}

void EmissionAntennaWidget::setupShaders()
{
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource)) {
        qWarning() << "EmissionAntennaWidget vertex shader error:" << m_program.log();
    }
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource)) {
        qWarning() << "EmissionAntennaWidget fragment shader error:" << m_program.log();
    }
    if (!m_program.link()) {
        qWarning() << "EmissionAntennaWidget link error:" << m_program.log();
    }
}

void EmissionAntennaWidget::setupFullscreenQuad()
{
    static constexpr GLfloat kQuadVertices[] = {
        // x, y, u, v
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    m_vao.create();
    m_vao.bind();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vbo.allocate(kQuadVertices, static_cast<int>(sizeof(kQuadVertices)));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<GLsizei>(sizeof(GLfloat)),
                          reinterpret_cast<void *>(2 * sizeof(GLfloat)));

    m_vbo.release();
    m_vao.release();
}

void EmissionAntennaWidget::spawnWave(float birthTimeSec)
{
    m_waves[static_cast<std::size_t>(m_waveWriteIndex)].birthTimeSec = birthTimeSec;
    m_waveWriteIndex = (m_waveWriteIndex + 1) % kMaxWaves;
}

void EmissionAntennaWidget::resetWaves()
{
    for (WaveState &wave : m_waves) {
        wave.birthTimeSec = -1.0f;
    }
}

void EmissionAntennaWidget::updateAnimationState(float nowSec, float dtSec)
{
    Q_UNUSED(dtSec)
    if (!m_transmitting) {
        return;
    }

    int spawnSafetyCounter = 0;
    while (nowSec >= m_nextWaveSpawnSec && spawnSafetyCounter < (kMaxWaves * 4)) {
        spawnWave(m_nextWaveSpawnSec);
        m_nextWaveSpawnSec += kWaveSpawnIntervalSec;
        ++spawnSafetyCounter;
    }
}

void EmissionAntennaWidget::ensureDebugLogger()
{
    if (m_debugLogger || !context()) {
        return;
    }

    m_debugLogger = new QOpenGLDebugLogger(this);
    if (!m_debugLogger->initialize()) {
        delete m_debugLogger;
        m_debugLogger = nullptr;
        return;
    }

    connect(m_debugLogger, &QOpenGLDebugLogger::messageLogged, this, [](const QOpenGLDebugMessage &msg) {
        qWarning().noquote() << "[OpenGL]" << msg.message();
    });
    m_debugLogger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
}

void EmissionAntennaWidget::cleanupGlResources()
{
    if (m_debugLogger) {
        m_debugLogger->stopLogging();
    }

    if (m_vbo.isCreated()) {
        m_vbo.destroy();
    }
    if (m_vao.isCreated()) {
        m_vao.destroy();
    }
    m_program.removeAllShaders();
}

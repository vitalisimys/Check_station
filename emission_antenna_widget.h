#ifndef EMISSION_ANTENNA_WIDGET_H
#define EMISSION_ANTENNA_WIDGET_H

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QOpenGLBuffer>
#include <QOpenGLDebugLogger>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <array>

/**
 * @brief OpenGL-виджет для индикации "выхода на мощность".
 *
 * Координатная система во фрагментном шейдере:
 * - начало координат в нижнем левом углу (pixel-space),
 * - ось X направлена вправо,
 * - ось Y направлена вверх.
 *
 * Ножка + залитый круг передатчика (SDF), волны — полукруги
 * (верхняя половина окружности) от точки над передатчиком.
 */
class EmissionAntennaWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit EmissionAntennaWidget(QWidget *parent = nullptr);
    ~EmissionAntennaWidget() override;

public slots:
    /**
     * @brief Потокобезопасный запуск передачи (можно вызывать из worker-thread).
     *
     * При вызове:
     * - сбрасывается фаза анимации,
     * - очищается буфер волн,
     * - первая волна появляется сразу (без визуального "провала" в начале).
     */
    void startTransmission();

    /**
     * @brief Потокобезопасная остановка передачи.
     */
    void stopTransmission();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void timerEvent(QTimerEvent *event) override;

private:
    struct WaveState {
        float birthTimeSec = -1.0f; // < 0 => слот неактивен
    };

    static constexpr int kMaxWaves = 16;                 // Лимит одновременных волн (uniform-массив).
    static constexpr int kFrameIntervalMs = 16;          // Целевой FPS ~60.
    static constexpr float kWaveSpawnIntervalSec = 0.30f;// Интервал генерации новых волн.
    static constexpr float kWaveSpeedPxPerSec = 120.0f;  // Скорость роста радиуса (px/s).
    static constexpr float kWaveMaxRadiusPx = 300.0f;    // Максимальный радиус (px), после которого волна исчезает.
    static constexpr float kWaveThicknessPx = 3.4f;      // Толщина линии волны (логич. px, с DPR в шейдере).

    void setupShaders();
    void setupFullscreenQuad();
    void spawnWave(float birthTimeSec);
    void resetWaves();
    void updateAnimationState(float nowSec, float dtSec);
    void ensureDebugLogger();
    void cleanupGlResources();

    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo { QOpenGLBuffer::VertexBuffer };
    QOpenGLDebugLogger *m_debugLogger = nullptr;

    QBasicTimer m_frameTimer;
    QElapsedTimer m_elapsedTimer;

    std::array<WaveState, kMaxWaves> m_waves {};
    int m_waveWriteIndex = 0;
    float m_nextWaveSpawnSec = 0.0f;
    float m_lastFrameSec = 0.0f;
    bool m_transmitting = false;
};

#endif // EMISSION_ANTENNA_WIDGET_H

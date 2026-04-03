#pragma once

#include <QMainWindow>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <QDoubleSpinBox>
#include <QPushButton>

class QEvent;
class QObject;

class QComboBox;
class QSlider;
class QLabel;

QT_BEGIN_NAMESPACE
namespace Ui { class HeatmapWindow; }
QT_END_NAMESPACE

class HeatmapWindow : public QMainWindow
{
public:
    struct MeasurementPoint {
        int x = 0;
        int y = 0;
        QVector<double> spectrum;
    };

    explicit HeatmapWindow(const QString& csvPath, QWidget* parent = nullptr);
    explicit HeatmapWindow(const QString& csvPath, int roiMinIdx, int roiMaxIdx, QWidget* parent = nullptr);
    static HeatmapWindow* createLazy(QWidget* parent = nullptr);
    void startLoadAsync(const QString& csvPath, int roiMinIdx, int roiMaxIdx);
    ~HeatmapWindow();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initUiForLazy();
    void finishLoadOnUiThread(int roiMinIdx, int roiMaxIdx);
    void scheduleRender();

    QString m_pendingCsv;
    int m_pendingRoiMin = 0;
    int m_pendingRoiMax = -1;
    bool m_lazyMode = false;

    void onSliderChanged(int value);
    void onRoiChanged();
    void resetRoiToFull();

    int roiMinIndex() const;
    int roiMaxIndex() const;
    int roiLen() const;
    int roiClampedMin() const;
    int roiClampedMax() const;
    int globalIdxFromRoiIdx(int roiIdx) const;
    int roiIdxFromGlobalIdx(int globalIdx) const;

    bool loadPointFolderFromAnyFile(const QString& anyPointCsvPath);
    static bool parsePointFileName(const QString& fileName, int* xOut, int* yOut);
    static QStringList parsePointFileHeaderExposures(const QString& headerLine);

    bool loadCsv(const QString& csvPath);
    bool loadCsvFromPoints(const QVector<MeasurementPoint>& points);
    QVector<MeasurementPoint> loadCsvPoints(const QString& csvPath, bool* ok = nullptr) const;
    void rebuildFromCurrentPoints();
    void discoverExposureFiles(const QString& primaryCsvPath);

    void buildCube();
    void updateHeatmap();
    void updateSpectrum();

    void updateColorbar(double minV, double maxV);

    QImage makeHeatmapImage(const QVector<double>& slice, int nx, int ny);

    Ui::HeatmapWindow* ui = nullptr;

    QVector<MeasurementPoint> m_points;
    QVector<int> m_xValues;
    QVector<int> m_yValues;

    int m_nx = 0;
    int m_ny = 0;
    int m_spectrumLen = 0;

    // cube[z = lambdaIndex] -> intensity per (x,y) flattened
    QVector<QVector<double>> m_cube;
    QMutex m_cubeMutex;

    int m_currentLambda = 0;

    int m_currentRoiMin = 0;
    int m_currentRoiMax = 0;

    // UI widgets (built in code)
    QSlider* m_slider = nullptr;
    QLabel* m_heatmapLabel = nullptr;
    QLabel* m_colorbarLabel = nullptr;
    QLabel* m_spectrumLabel = nullptr;
    QComboBox* m_exposureCombo = nullptr;

    QDoubleSpinBox* m_roiMinSpin = nullptr;
    QDoubleSpinBox* m_roiMaxSpin = nullptr;
    QPushButton* m_roiResetBtn = nullptr;
    QComboBox* m_colormapCombo = nullptr;

    QImage m_lastHeatmapImage;
    QImage m_lastSpectrumImage;
    int m_lastHeatmapLambda = -1;
    int m_lastHeatmapNx = 0;
    int m_lastHeatmapNy = 0;
    QSize m_lastHeatmapTargetSize;
    int m_lastSpectrumRoiMin = -1;
    int m_lastSpectrumRoiMax = -1;
    QSize m_lastSpectrumTargetSize;
    QVector<double> m_lastSpectrumMean;

    bool m_deferredInitialRenderDone = false;

    QTimer* m_resizeRenderTimer = nullptr;
    std::atomic_bool m_renderWorkerRunning{ false };
    std::atomic_bool m_renderPending{ false };

    double m_dataMin = 0.0;
    double m_dataMax = 1.0;

    QColor mapColor(int c) const;

    // Data sources (Primary + per-exposure)
    struct ExposureSource {
        QString label;
        QString csvPath;
    };
    QVector<ExposureSource> m_sources;
    QString m_currentCsv;

    // New per-point folder format
    bool m_isPointFolder = false;
    QString m_pointFolderPath;
    QStringList m_pointExposureLabels;
    int m_pointExposureColumn = 1; // 1..N (0 is lambda)
};

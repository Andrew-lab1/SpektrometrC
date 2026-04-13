#pragma once
#include <QtWidgets/QMainWindow>
#include <atomic>
#include <QMutex>
#include <QPixmap>
#include <QImage>
#include <QTimer>
#include <QThread>
#include <QWidget>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QDateTime>
#include <QSerialPort>
#ifdef Q_OS_WIN
#include <Windows.h>
#endif
#include "Options.h"
#include "ui_Spektrometr.h"

namespace cv { class Mat; }
class CameraWorker;
class Spektrometr : public QMainWindow
{
    Q_OBJECT
public:
    Spektrometr(QWidget *parent = nullptr);
    ~Spektrometr();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private slots:
    void startSequence();
    void stopSequence();
    void refreshResults();
    void openSelectedMeasurement();
    void exportAllMeasurements();
    void deleteAllMeasurements();
    void deleteSelectedMeasurement();
private:
    void pauseSequence(const QString& reason);
    void resumeSequence();
    void showSequencePauseDialog(const QString& reason);
    void tryAutoResumeSequence();
    static QImage renderCenteredTextImage(const QSize& size, const QString& text, const QColor& color, const QColor& bg = Qt::black);
    QWidget* m_loadingOverlay = nullptr;
    QLabel* m_loadingLabel = nullptr;
    void ensureLoadingOverlay();
    void setLoadingOverlayVisible(bool visible, const QString& text = QString());
    void loadOptionsToUi();
    void init();
    void loop();
    void updateLoop();
    void tickSpectrum(bool force = false);
    void tickPixelink();
    void tickPorts();
    void startPixelink();
    QWidget* m_toastHost = nullptr;
    QVBoxLayout* m_toastLayout = nullptr;
    std::atomic_bool m_spectrumUpdateScheduled{ false };
    Ui::SpektrometrClass ui;
    void appendLog(const QString& line);
    qint64 m_lastToastMs = 0;
    QString m_lastToastMessage;
    std::atomic_bool m_pixelinkWorkerRunning{ false };
    std::atomic_bool m_pixelinkStopRequested{ false };
    QMutex m_pixelinkFrameMutex;
    QMutex m_pixelinkCameraMutex;
    QImage m_pixelinkLatestFrame;
    std::atomic<quint64> m_pixelinkLatestFrameNumber{ 0 };
    std::atomic_bool m_spectrumWorkerRunning{ false };
    std::atomic_bool m_spectrumRenderPending{ false };
    QMutex m_spectrumPixmapMutex;
    QPixmap m_spectrumLatestPixmap;
    QMutex m_spectrumImageMutex;
    QImage m_spectrumLatestImage;
    std::atomic_bool m_resultsScanRunning{ false };
    std::atomic_bool m_resultsRefreshPending{ false };
    QLabel* m_connectionStatusLabel = nullptr;
    void closeMotorPorts();
    // canStartSequence removed; use check_hardware() instead
    void setSequenceButtonsEnabled(bool running);
    void ensureConnectionStatusLabel();
    void updateConnectionStatusUi();
    struct SequencePlanPoint {
        int ix = 0;
        int iy = 0;
        int xUm = 0;
        int yUm = 0;
    };
    QVector<SequencePlanPoint> m_movementMap;
    int m_sequencePointIndex = 0;
    QString m_sequenceSessionFolder;
    bool m_sequenceSnake = true;
    struct SequenceRunSnapshot {
        int roiMin = 0;
        int roiMax = 0;
        QVector<double> exposures;
    };
    // Stage tracking relative to sequence start (center) in micrometers.
    std::atomic_int m_stageOffsetXUm{ 0 };
    std::atomic_int m_stageOffsetYUm{ 0 };
    bool m_stageHasReference = false;
    void returnStageToSequenceStart();
    void runSequenceLoop(SequenceRunSnapshot snapshot);
    void launchSequenceWorker(SequenceRunSnapshot snapshot);
    bool waitForSequenceRunning();
    void finalizeSequenceSuccess();
    // Simplified helper API requested for a clearer sequence flow
    bool check_hardware(QString* reasonOut = nullptr);
    bool portsOpen() const;
    bool pixelinkOpen() const;
    void make_movement_map();
    void preview_map(SequenceRunSnapshot snapshot);
    void apply_roi(int roiMin, int roiMax);
    void showSpectrumCalibrationDialog();
    void showBrightnessCalibrationDialog();
    void showFunctionCalibrationDialog();
    void runCalibrationMode(int modeIndex);
    bool move(int dxUm, int dyUm, QString* errOut);
    // legacy save_spec removed; use per-exposure files + save_spec_from_files
    bool save_spec_from_files(const SequencePlanPoint& pt, const QVector<double>& exposuresMs, int completedExposureCount, QString* errOut);
    bool openSerialPort(QSerialPort*& port, QString& openName, const QString& wantName, const char* which);
    void showLoading(const QString& text = QString());
    void hideLoading();
    bool hasSpectrumCalibration() const;
    double wavelengthForPixel(double pixel) const;
    void setSpectrumCalibration(double pixel1, double nm1, double pixel2, double nm2);
    static cv::Mat qImageToCvMat(const QImage& image);
    // Index into movement map / plan
    static QVector<double> spectrumFromFrame(const QImage& src, int roiMin, int roiMax);
    std::atomic_bool m_sequenceRunning{ false };
    std::atomic_bool m_sequencePaused{ false };
    QThread* m_sequenceWorkerThread = nullptr;
    QTimer* m_sequenceReconnectTimer = nullptr;
    bool m_pauseDialogOpen = false;
#ifdef Q_OS_WIN
    QSerialPort* m_portX = nullptr;
    QSerialPort* m_portY = nullptr;
    QString m_openPortX;
    QString m_openPortY;
#endif
    QThread* m_cameraThread = nullptr;
    CameraWorker* m_cameraWorker = nullptr;
    // Background file worker thread for progress/CSV writes
    QThread* m_fileWorkerThread = nullptr;
    QObject* m_fileWorker = nullptr;
    Options m_options;
    QString m_optionsPath;
    double spectrum_range_min = 0.0;
    double spectrum_range_max = 2048.0;
    double spectrum_cal_pixel1 = 0.0;
    double spectrum_cal_nm1 = 0.0;
    double spectrum_cal_pixel2 = 2048.0;
    double spectrum_cal_nm2 = 2048.0;
    void saveOptions();
    void refreshPortLists();
    void stopPixelink();
    QTimer* m_pixelinkTimer = nullptr;
    QTimer* m_pixelinkReconnectTimer = nullptr;
    QTimer* m_portTimer = nullptr;
    bool m_pixelinkConnectInProgress = false;
    qint64 m_pixelinkNextConnectAllowedMs = 0;
    bool m_pixelinkNotConnectedLogged = false;
    bool m_portsNotConnectedLogged = false;
    qint64 m_portsNextConnectAllowedMs = 0;
    std::atomic_bool m_pixelinkRenderPending{ false };
    QVector<double> m_sequenceExposureTimesMs;
#ifdef Q_OS_WIN
    HANDLE m_pixelinkCamera = nullptr;
    bool m_pixelinkStreaming = false;
#endif
};

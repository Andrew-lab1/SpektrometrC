#include "Spektrometr.h"
#include "HeatmapWindow.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QPixmap>
#include <QStringList>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QTimer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDirIterator>
#include <QListWidgetItem>
#include <QMutexLocker>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QPointer>
#include <QElapsedTimer>

#include <thread>
#include <functional>
#include <vector>
#include <cmath>
#include <limits>

#if __has_include(<opencv2/opencv.hpp>)
#include <opencv2/opencv.hpp>
#define SPEKTROMETR_HAS_OPENCV 1
#else
#define SPEKTROMETR_HAS_OPENCV 0
#endif

#if SPEKTROMETR_HAS_OPENCV
cv::Mat Spektrometr::qImageToCvMat(const QImage& image)
{
    if (image.isNull()) {
        return cv::Mat();
    }

    QImage converted = image.convertToFormat(QImage::Format_ARGB32);
    cv::Mat mat(converted.height(), converted.width(), CV_8UC4, const_cast<uchar*>(converted.bits()), converted.bytesPerLine());
    return mat.clone();
}

static QImage cvMatToQImage(const cv::Mat& mat)
{
    if (mat.empty()) {
        return {};
    }

    if (mat.type() == CV_8UC4) {
        QImage img(mat.data, mat.cols, mat.rows, int(mat.step), QImage::Format_ARGB32);
        return img.copy();
    }
    if (mat.type() == CV_8UC3) {
        QImage img(mat.data, mat.cols, mat.rows, int(mat.step), QImage::Format_RGB888);
        return img.rgbSwapped().copy();
    }
    if (mat.type() == CV_8UC1) {
        QImage img(mat.data, mat.cols, mat.rows, int(mat.step), QImage::Format_Grayscale8);
        return img.copy();
    }
    return {};
}
#endif

QImage Spektrometr::renderCenteredTextImage(const QSize& size, const QString& text, const QColor& color, const QColor& bg)
{
    const int w = qMax(1, size.width());
    const int h = qMax(1, size.height());
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(bg);
    QPainter painter(&img);
    painter.setPen(color);
    painter.drawText(QRect(0, 0, w, h), Qt::AlignCenter, text);
    return img;
}

static QString normalizedSerialPortName(const QString& portName)
{
    QString name = portName.trimmed();
    const int separator = name.indexOf(QLatin1Char(' '));
    if (separator > 0) {
        name = name.left(separator).trimmed();
    }
    return name;
} 

static QString formatPortStatus(const QString& axis, const QString& portName, bool connected)
{
    const QString displayName = normalizedSerialPortName(portName).isEmpty()
        ? QStringLiteral("n/a")
        : normalizedSerialPortName(portName);
    return QStringLiteral("%1: %2 (%3)").arg(axis, displayName, connected ? QStringLiteral("open") : QStringLiteral("closed"));
}

static QString formatPixelinkStatus(bool connectInProgress, bool connected, bool retryPending)
{
    if (connectInProgress) {
        return QStringLiteral("Camera: connecting");
    }
    if (connected) {
        return QStringLiteral("Camera: streaming");
    }
    return retryPending ? QStringLiteral("Camera: retry pending") : QStringLiteral("Camera: disconnected");
}

bool Spektrometr::openSerialPort(QSerialPort*& port, QString& openName, const QString& wantName, const char* which)
{
    const QString normalizedWantName = normalizedSerialPortName(wantName);
#ifdef Q_OS_WIN
    if (port && port->isOpen() && normalizedSerialPortName(openName) == normalizedWantName) return true;

    if (port) {
        port->close();
        port->deleteLater();
        port = nullptr;
        openName.clear();
    }

    auto* p = new QSerialPort(this);
    p->setPortName(normalizedWantName);
    p->setBaudRate(QSerialPort::Baud9600);
    p->setDataBits(QSerialPort::Data8);
    p->setParity(QSerialPort::NoParity);
    p->setStopBits(QSerialPort::OneStop);
    p->setFlowControl(QSerialPort::NoFlowControl);

    if (!p->open(QIODevice::ReadWrite)) {
        const QString err = p->errorString();
        p->deleteLater();
        if (!m_portsNotConnectedLogged) {
            appendLog(QStringLiteral("Open Port %1 failed (%2): %3").arg(QString::fromLatin1(which), wantName, err));
            m_portsNotConnectedLogged = true;
        }
        m_portsNextConnectAllowedMs = QDateTime::currentMSecsSinceEpoch() + 3000;
        setSequenceButtonsEnabled(m_sequenceRunning.load());
        updateConnectionStatusUi();
        return false;
    }

    port = p;
    openName = wantName;
    m_portsNotConnectedLogged = false;
    m_portsNextConnectAllowedMs = 0;
    setSequenceButtonsEnabled(m_sequenceRunning.load());
    updateConnectionStatusUi();
    return true;
#else
    Q_UNUSED(port);
    Q_UNUSED(openName);
    Q_UNUSED(wantName);
    Q_UNUSED(which);
    return false;
#endif
}

void Spektrometr::showLoading(const QString& text)
{
    ensureLoadingOverlay();
    setLoadingOverlayVisible(true, text);
}

void Spektrometr::hideLoading()
{
    setLoadingOverlayVisible(false);
}

bool Spektrometr::save_spec_from_files(const SequencePlanPoint& pt, const QVector<double>& exposuresMs, QString* errOut)
{
    if (errOut) errOut->clear();
    const int nExp = exposuresMs.size();
    if (nExp <= 0) {
        if (errOut) *errOut = QStringLiteral("No exposures");
        return false;
    }

    QDir folder(m_sequenceSessionFolder);
    QVector<QVector<double>> spectra;
    spectra.reserve(nExp);

    auto exposurePath = [&folder, &pt](int exposureIndex) {
        return folder.absoluteFilePath(QStringLiteral("pt_x%1_y%2_e%3.csv").arg(pt.ix).arg(pt.iy).arg(exposureIndex));
    };

    auto readSpectrumFile = [this](const QString& path, QVector<double>& spec, QString* errOut) -> bool {
        if (errOut) errOut->clear();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errOut) *errOut = QStringLiteral("Missing exposure file: %1").arg(path);
            return false;
        }

        QTextStream in(&f);
        in.readLine();
        int lineNo = 1;
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            ++lineNo;
            if (line.isEmpty()) continue;

            const QStringList parts = line.split(',');
            if (parts.size() < 2) continue;

            QString valStr = parts.at(1).trimmed();
            bool ok = false;
            double v = QLocale::c().toDouble(valStr, &ok);
            if (!ok) v = QLocale().toDouble(valStr, &ok);
            if (!ok) {
                QString cleaned;
                cleaned.reserve(valStr.size());
                for (QChar ch : valStr) {
                    const QChar c = ch;
                    if ((c >= '0' && c <= '9') || c == 'e' || c == 'E' || c == '+' || c == '-' || c == '.' || c == ',') {
                        cleaned.append(c);
                    }
                }
                cleaned.replace(',', '.');
                v = cleaned.toDouble(&ok);
            }

            if (!ok) {
                QMetaObject::invokeMethod(this, [this, path, lineNo, valStr]() {
                    appendLog(QStringLiteral("Failed to parse value in %1 at line %2: '%3'").arg(path).arg(lineNo).arg(valStr));
                }, Qt::QueuedConnection);
                spec.push_back(0.0);
            } else {
                spec.push_back(v);
            }
        }
        return true;
    };

    for (int e = 0; e < nExp; ++e) {
        const QString path = exposurePath(e + 1);
        QVector<double> spec;
        if (!readSpectrumFile(path, spec, errOut)) {
            return false;
        }
        spectra.push_back(std::move(spec));
    }

    // Determine maximum spectrum length among per-exposure files. This
    // tolerates missing/short files produced by placeholders — shorter
    // spectra will be padded with zeros when creating the combined CSV.
    int maxLen = 0;
    for (int i = 0; i < spectra.size(); ++i) {
        maxLen = qMax(maxLen, spectra[i].size());
    }
    if (maxLen <= 0) {
        // No data in any per-exposure file — report error so sequence does not
        // proceed (placeholders only are not valid measurement data).
        QStringList files;
        for (int e = 0; e < nExp; ++e) {
            files.append(exposurePath(e + 1));
        }
        if (errOut) *errOut = QStringLiteral("Empty spectra (no data rows in per-exposure files): %1").arg(files.join(", "));
        return false;
    }
    const int nLam = maxLen;

    // Write combined CSV (overwrite)
    const QString outPath = folder.absoluteFilePath(QStringLiteral("pt_x%1_y%2.csv").arg(pt.ix).arg(pt.iy));
    QFile of(outPath);
    if (!of.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errOut) *errOut = QStringLiteral("Failed to open output CSV: %1").arg(outPath);
        return false;
    }
    QTextStream out(&of);
    out << "lambda_idx";
    for (int e = 0; e < nExp; ++e) out << ",t_" << (e + 1) << "_ms";
    out << "\n";
    for (int i = 0; i < nLam; ++i) {
        out << i;
        for (int e = 0; e < nExp; ++e) {
            const double v = (i < spectra[e].size()) ? spectra[e][i] : 0.0;
            out << "," << QString::number(v, 'f', 3);
        }
        out << "\n";
    }
    of.close();
    return true;
}

bool Spektrometr::check_hardware(QString* reasonOut)
{
    if (reasonOut) reasonOut->clear();

    if (!portsOpen()) {
        if (reasonOut) *reasonOut = QStringLiteral("Motors not connected");
        return false;
    }

    if (!pixelinkOpen()) {
        if (reasonOut) *reasonOut = QStringLiteral("PixeLink not connected");
        return false;
    }

    return true;
}

bool Spektrometr::portsOpen() const
{
#ifdef Q_OS_WIN
    return (m_portX != nullptr && m_portX->isOpen() && m_portY != nullptr && m_portY->isOpen());
#else
    return false;
#endif
}

bool Spektrometr::pixelinkOpen() const
{
#if HAVE_PIXELINK_SDK
    return (m_pixelinkCamera != nullptr && m_pixelinkStreaming);
#else
    return false;
#endif
}

void Spektrometr::make_movement_map()
{
    // Generate movement map from UI settings (fallback to options)
    m_movementMap.clear();

    const int wUm = ui.spinScanWidth ? qMax(1, ui.spinScanWidth->value()) : qMax(1, m_options.width);
    const int hUm = ui.spinScanHeight ? qMax(1, ui.spinScanHeight->value()) : qMax(1, m_options.height);
    const int stepXum = ui.spinStepX ? qMax(1, ui.spinStepX->value()) : qMax(1, int(m_options.step_x));
    const int stepYum = ui.spinStepY ? qMax(1, ui.spinStepY->value()) : qMax(1, int(m_options.step_y));

    const int nx = qMax(1, (wUm / stepXum) + 1);
    const int ny = qMax(1, (hUm / stepYum) + 1);
    const int cx = nx / 2;
    const int cy = ny / 2;

    for (int iy = 0; iy < ny; ++iy) {
        const bool rev = m_sequenceSnake && ((iy % 2) == 1);
        for (int ix0 = 0; ix0 < nx; ++ix0) {
            const int ix = rev ? (nx - 1 - ix0) : ix0;
            SequencePlanPoint p;
            p.ix = ix;
            p.iy = iy;
            p.xUm = (ix - cx) * stepXum;
            p.yUm = (iy - cy) * stepYum;
            m_movementMap.push_back(p);
        }
    }

}

void Spektrometr::preview_map(SequenceRunSnapshot snapshot)
{
    // Preview path: go from center to the corner, outline the area, then return to center.
    QString reason;
    if (!check_hardware(&reason)) {
        pauseSequence(reason.isEmpty() ? QStringLiteral("Hardware not ready") : reason);
        return;
    }

    // Build corner-to-corner move list (no step-by-step edge tracing).
    // Use existing movement map generation to avoid duplicating nx/ny/step logic.
    make_movement_map();

    // Derive grid dimensions and step sizes from movement map.
    int nx = 0, ny = 0;
    for (const auto& p : m_movementMap) {
        nx = qMax(nx, p.ix + 1);
        ny = qMax(ny, p.iy + 1);
    }
    if (nx <= 0) nx = 1;
    if (ny <= 0) ny = 1;

    // Determine step sizes (fall back to options if not available)
    int stepXum = qMax(1, int(m_options.step_x));
    int stepYum = qMax(1, int(m_options.step_y));
    // try to compute from movement map points if possible
    SequencePlanPoint p00{0,0,0,0};
    SequencePlanPoint p10{0,0,0,0};
    SequencePlanPoint p01{0,0,0,0};
    for (const auto& p : m_movementMap) {
        if (p.ix == 0 && p.iy == 0) p00 = p;
        if (p.ix == 1 && p.iy == 0) p10 = p;
        if (p.ix == 0 && p.iy == 1) p01 = p;
    }
    if (nx > 1) stepXum = qAbs(p10.xUm - p00.xUm);
    if (ny > 1) stepYum = qAbs(p01.yUm - p00.yUm);

    const int cx = nx / 2;
    const int cy = ny / 2;

    struct MoveUm { int dx = 0; int dy = 0; };
    QVector<MoveUm> moves;
    moves.reserve(6);

    // Center -> top-left corner.
    moves.push_back({ -(cx * stepXum), -(cy * stepYum) });
    // Top-left -> top-right.
    moves.push_back({ (nx - 1) * stepXum, 0 });
    // Top-right -> bottom-right.
    moves.push_back({ 0, (ny - 1) * stepYum });
    // Bottom-right -> bottom-left.
    moves.push_back({ -((nx - 1) * stepXum), 0 });
    // Bottom-left -> top-left.
    moves.push_back({ 0, -((ny - 1) * stepYum) });
    // Top-left -> center.
    moves.push_back({ cx * stepXum, cy * stepYum });

    auto idx = std::make_shared<int>(0);
    auto cont = std::make_shared<std::function<void()>>();
    *cont = [this, moves, idx, cont, snapshot]() {
        if (!m_sequenceRunning.load() || m_sequencePaused.load()) return;
        if (!portsOpen()) {
            pauseSequence(QStringLiteral("Motors disconnected"));
            return;
        }

        if (*idx >= moves.size()) {
            const auto res = QMessageBox::question(this, tr("Sequence"), tr("Preview done. Does the area match?\n\nYes = start measurement\nNo = stop"));
            if (res != QMessageBox::Yes) {
                stopSequence();
                return;
            }
            m_sequenceRunning.store(true);
            m_sequencePaused.store(false);
            launchSequenceWorker(snapshot);
            return;
        }

        const MoveUm m = moves[*idx];
        (*idx)++;
        QString err;
        if (!move(m.dx, m.dy, &err)) {
            pauseSequence(err);
            return;
        }

        QTimer::singleShot(220, this, [cont]() { (*cont)(); });
    };

    (*cont)();
}

void Spektrometr::apply_roi(int roiMin, int roiMax)
{
    m_options.spectrum_range_min = roiMin;
    m_options.spectrum_range_max = roiMax;
    if (ui.spinRoiMin) ui.spinRoiMin->setValue(m_options.spectrum_range_min);
    if (ui.spinRoiMax) ui.spinRoiMax->setValue(m_options.spectrum_range_max);
    ::saveOptions(m_optionsPath, m_options);
    appendLog(QStringLiteral("ROI applied: %1 - %2").arg(m_options.spectrum_range_min).arg(m_options.spectrum_range_max));
    tickSpectrum();
}

bool Spektrometr::move(int dxUm, int dyUm, QString* errOut)
{
    if (errOut) errOut->clear();
#ifdef Q_OS_WIN
    if (dxUm == 0 && dyUm == 0) return true;
    if (!portsOpen()) {
        if (errOut) *errOut = QStringLiteral("Motors disconnected");
        return false;
    }

    const int dxP = (dxUm == 0) ? 0 : ((dxUm > 0) ? qMax(1, dxUm / 2) : -qMax(1, (-dxUm) / 2));
    const int dyP = (dyUm == 0) ? 0 : ((dyUm > 0) ? qMax(1, dyUm / 2) : -qMax(1, (-dyUm) / 2));

    QString err;
    if (dxP != 0) {
        const QByteArray cmd = (dxP > 0 ? QByteArray("M:1+P") : QByteArray("M:1-P")) + QByteArray::number(qAbs(dxP)) + "\r\nG:\r\n";
        if (!m_portX || !m_portX->isOpen()) {
            err = QStringLiteral("X not open");
            if (errOut) *errOut = err;
            return false;
        }
        if (m_portX->write(cmd) != cmd.size()) {
            err = QStringLiteral("X write failed: %1").arg(m_portX->errorString());
            if (errOut) *errOut = err;
            return false;
        }
        m_stageOffsetXUm.fetch_add(dxUm);
    }
    if (dyP != 0) {
        const QByteArray cmd = (dyP > 0 ? QByteArray("M:1+P") : QByteArray("M:1-P")) + QByteArray::number(qAbs(dyP)) + "\r\nG:\r\n";
        if (!m_portY || !m_portY->isOpen()) {
            err = QStringLiteral("Y not open");
            if (errOut) *errOut = err;
            return false;
        }
        if (m_portY->write(cmd) != cmd.size()) {
            err = QStringLiteral("Y write failed: %1").arg(m_portY->errorString());
            if (errOut) *errOut = err;
            return false;
        }
        m_stageOffsetYUm.fetch_add(dyUm);
    }
    return true;
#else
    Q_UNUSED(dxUm);
    Q_UNUSED(dyUm);
    if (errOut) *errOut = QStringLiteral("Motor control not available on this OS");
    return false;
#endif
}

void Spektrometr::setLoadingOverlayVisible(bool visible, const QString& text)
{
    ensureLoadingOverlay();
    if (!m_loadingOverlay) return;

    if (!text.isEmpty() && m_loadingLabel) {
        m_loadingLabel->setText(text);
    }

    m_loadingOverlay->setGeometry(centralWidget()->rect());
    m_loadingOverlay->raise();
    m_loadingOverlay->setVisible(visible);
}

static QDir resolveMeasurementDataDir()
{
    const QStringList bases = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath(),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(".."),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.."),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../..")
    };

    for (const QString& base : bases) {
        const QDir d(QDir(base).absoluteFilePath("measurement_data"));
        if (d.exists()) {
            return d;
        }
    }

    return QDir(QDir::current().absoluteFilePath("measurement_data"));
}

struct MeasurementListEntry {
    QString displayName;
    QString folderPath;
    QString firstCsvPath;
    int pointCount = 0;
};

static QVector<MeasurementListEntry> collectMeasurementListEntries(const QString& rootPath)
{
    QVector<MeasurementListEntry> entries;

    auto addFolder = [&entries](const QString& dirPath) {
        QDir d(dirPath);
        const auto pointFiles = d.entryInfoList(QStringList() << QStringLiteral("pt_x*_y*.csv"), QDir::Files | QDir::Readable, QDir::Name);
        if (pointFiles.isEmpty()) {
            return;
        }

        QString displayName = QFileInfo(dirPath).fileName();
        if (displayName.isEmpty()) {
            displayName = QStringLiteral("measurement_data");
        }

        MeasurementListEntry entry;
        entry.displayName = displayName;
        entry.folderPath = dirPath;
        entry.firstCsvPath = pointFiles.first().absoluteFilePath();
        entry.pointCount = pointFiles.size();
        entries.push_back(std::move(entry));
    };

    addFolder(rootPath);
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        addFolder(it.next());
    }

    return entries;
}

#ifdef Q_OS_WIN
#include <QSettings>
#include <windows.h>
#endif

#ifdef Q_OS_WIN
#if __has_include(<PixeLINKApi.h>)
#include <PixeLINKApi.h>
#define HAVE_PIXELINK_SDK 1
#else
#define HAVE_PIXELINK_SDK 0
#endif
#else
#define HAVE_PIXELINK_SDK 0
#endif

#if HAVE_PIXELINK_SDK
static bool setPixelinkExposure(HANDLE cam, double expMs, QString* errOut)
{
    if (errOut) errOut->clear();
    if (cam == nullptr) return false;

    const U32 flags = FEATURE_FLAG_MANUAL;
    float exposure = float(expMs / 1000.0);
    U32 nParams = 1;
    const PXL_RETURN_CODE rc = PxLSetFeature(cam, FEATURE_EXPOSURE, flags, nParams, &exposure);
    if (!API_SUCCESS(rc)) {
        if (errOut) *errOut = QStringLiteral("PxLSetFeature(FEATURE_EXPOSURE) failed (0x%1)").arg(QString::number(rc, 16));
        return false;
    }
    return true;
}

static bool waitForPixelinkExposureApplied(HANDLE cam, double expMs, QString* errOut)
{
    if (errOut) errOut->clear();
    if (cam == nullptr) return false;

    const float targetExposure = float(expMs / 1000.0);
    const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 1500;
    QString lastError;

    while (QDateTime::currentMSecsSinceEpoch() <= deadlineMs) {
        U32 flags = 0;
        U32 count = 1;
        float currentExposure = 0.0f;
        const PXL_RETURN_CODE rc = PxLGetFeature(cam, FEATURE_EXPOSURE, &flags, &count, &currentExposure);
        if (API_SUCCESS(rc)) {
            const double tolerance = qMax(0.002, double(targetExposure) * 0.01);
            if (qAbs(double(currentExposure) - double(targetExposure)) <= tolerance) {
                return true;
            }
        } else {
            lastError = QStringLiteral("PxLGetFeature(FEATURE_EXPOSURE) failed (0x%1)").arg(QString::number(rc, 16));
        }

        QThread::msleep(20);
    }

    if (errOut) {
        if (!lastError.isEmpty()) {
            *errOut = lastError;
        } else {
            *errOut = QStringLiteral("Exposure did not settle to %1 ms").arg(QString::number(expMs, 'f', 1));
        }
    }
    return false;
}

static bool setPixelinkGain(HANDLE cam, double gain, QString* errOut)
{
    if (errOut) errOut->clear();
    if (cam == nullptr) return false;

    const U32 flags = FEATURE_FLAG_MANUAL;
    float g = float(gain);
    U32 nParams = 1;
    const PXL_RETURN_CODE rc = PxLSetFeature(cam, FEATURE_GAIN, flags, nParams, &g);
    if (!API_SUCCESS(rc)) {
        if (errOut) *errOut = QStringLiteral("PxLSetFeature(FEATURE_GAIN) failed (0x%1)").arg(QString::number(rc, 16));
        return false;
    }
    return true;
}

#endif

Spektrometr::Spektrometr(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    init();
    loop();
}

void Spektrometr::init()
{
    // GUI setup
    // Ensure we get notified when the window is shown/resized (initial layout pass).
    this->installEventFilter(this);
    if (centralWidget()) {
        centralWidget()->installEventFilter(this);
    }

    if (ui.labelPixelinkPreview) {
        ui.labelPixelinkPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        ui.labelPixelinkPreview->setText(tr("PixeLink preview"));
        ui.labelPixelinkPreview->setAlignment(Qt::AlignCenter);
        ui.labelPixelinkPreview->setScaledContents(true);
        ui.labelPixelinkPreview->setMinimumSize(QSize(320, 240));
        ui.labelPixelinkPreview->setStyleSheet(QStringLiteral("QLabel{background:#101114;color:#d8d8d8;border:1px solid rgba(255,255,255,30);}"));
    }

    if (ui.labelSpectrumPlaceholder) {
        ui.labelSpectrumPlaceholder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        ui.labelSpectrumPlaceholder->clear();
        ui.labelSpectrumPlaceholder->setScaledContents(true);
        ui.labelSpectrumPlaceholder->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        ui.labelSpectrumPlaceholder->setContentsMargins(0, 0, 0, 0);
        ui.labelSpectrumPlaceholder->setStyleSheet(QStringLiteral("QLabel{margin:0px; padding:0px; border:0px;}"));
        ui.labelSpectrumPlaceholder->installEventFilter(this);
    }

    // Options and ports
    m_optionsPath = QDir::current().absoluteFilePath("options.json");
    m_options = loadOptions(m_optionsPath);
    refreshPortLists();
    loadOptionsToUi();
#ifdef Q_OS_WIN
    {
        const QString wantX = ui.comboPortX ? normalizedSerialPortName(ui.comboPortX->currentText()) : normalizedSerialPortName(m_options.port_x);
        const QString wantY = ui.comboPortY ? normalizedSerialPortName(ui.comboPortY->currentText()) : normalizedSerialPortName(m_options.port_y);
        openSerialPort(m_portX, m_openPortX, wantX, "X");
        openSerialPort(m_portY, m_openPortY, wantY, "Y");
    }
#endif

    // Signals
    if (ui.btnStartSequence) {
        connect(ui.btnStartSequence, &QPushButton::clicked, this, &Spektrometr::startSequence);
    }
    if (ui.btnStopSequence) {
        connect(ui.btnStopSequence, &QPushButton::clicked, this, &Spektrometr::stopSequence);
    }

    connect(ui.btnRefreshResults, &QPushButton::clicked, this, &Spektrometr::refreshResults);
    if (ui.btnExportAll) {
        connect(ui.btnExportAll, &QPushButton::clicked, this, &Spektrometr::exportAllMeasurements);
    }
    if (ui.btnDeleteAll) {
        connect(ui.btnDeleteAll, &QPushButton::clicked, this, &Spektrometr::deleteAllMeasurements);
    }
    if (ui.btnDeleteSelected) {
        connect(ui.btnDeleteSelected, &QPushButton::clicked, this, &Spektrometr::deleteSelectedMeasurement);
    }
    if (ui.btnOpenMeasurement) {
        connect(ui.btnOpenMeasurement, &QPushButton::clicked, this, &Spektrometr::openSelectedMeasurement);
    }
    if (ui.listMeasurements) {
        connect(ui.listMeasurements, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
            openSelectedMeasurement();
        });
    }

    if (ui.btnSaveSettings) {
        connect(ui.btnSaveSettings, &QPushButton::clicked, this, [this]() {
            refreshPortLists();
            saveOptions();
            if (::saveOptions(m_optionsPath, m_options)) {
                appendLog(QStringLiteral("Saved options.json"));
                // Reconnect motors right after saving port selection.
#ifdef Q_OS_WIN
                {
                    const QString wantX = ui.comboPortX ? normalizedSerialPortName(ui.comboPortX->currentText()) : normalizedSerialPortName(m_options.port_x);
                    const QString wantY = ui.comboPortY ? normalizedSerialPortName(ui.comboPortY->currentText()) : normalizedSerialPortName(m_options.port_y);
                    openSerialPort(m_portX, m_openPortX, wantX, "X");
                    openSerialPort(m_portY, m_openPortY, wantY, "Y");
                }
#endif
                setSequenceButtonsEnabled(false);
            } else {
                QMessageBox::warning(this, tr("Settings"), tr("Failed to save options.json"));
            }
        });
    }

    if (ui.spinExposureSpectrum) {
        connect(ui.spinExposureSpectrum, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            m_options.exposure_time_ms = ui.spinExposureSpectrum->value();
            ::saveOptions(m_optionsPath, m_options);
#if HAVE_PIXELINK_SDK
            QString err;
            if (!setPixelinkExposure(m_pixelinkCamera, m_options.exposure_time_ms, &err) && !err.isEmpty()) {
                appendLog(QStringLiteral("Exposure change failed: %1").arg(err));
            }
#endif
            tickSpectrum();
        });
    }

    if (ui.tabWidget && ui.tabSpectrum) {
        connect(ui.tabWidget, &QTabWidget::currentChanged, this, [this](int ix) {
            if (ui.tabWidget->widget(ix) != ui.tabSpectrum) return;
            tickSpectrum();
            tickPixelink();
        });
    }

    if (ui.spinGainSpectrum) {
        connect(ui.spinGainSpectrum, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            m_options.gain = ui.spinGainSpectrum->value();
            ::saveOptions(m_optionsPath, m_options);
#if HAVE_PIXELINK_SDK
            if (m_pixelinkCamera != nullptr && m_pixelinkStreaming) {
                QString err;
                if (!setPixelinkGain(m_pixelinkCamera, m_options.gain, &err) && !err.isEmpty()) {
                    appendLog(QStringLiteral("PixeLink gain set failed: %1").arg(err));
                }
            }
#endif
            tickSpectrum();
        });
    }

    if (ui.horizontalLayoutSpectrumControls) {
        auto* comboCalibration = new QComboBox(ui.tabSpectrum);
        comboCalibration->addItem(tr("Calibration: Brightness"));
        comboCalibration->addItem(tr("Calibration: Function"));
        ui.horizontalLayoutSpectrumControls->insertWidget(qMax(0, ui.horizontalLayoutSpectrumControls->count() - 1), comboCalibration);
        connect(comboCalibration, qOverload<int>(&QComboBox::activated), this, [this](int ix) {
            runCalibrationMode(ix);
        });
    }

    if (auto* edit = findChild<QLineEdit*>(QStringLiteral("editExposureSequence"))) {
        connect(edit, &QLineEdit::textChanged, this, [this](const QString& t) {
            QString err;
            const QString tt = t.trimmed();
            if (tt.isEmpty()) {
                m_sequenceExposureTimesMs.clear();
                return;
            }
            QVector<double> seq;
            const auto parts = tt.split(';', Qt::SkipEmptyParts);
            for (QString p : parts) {
                p = p.trimmed();
                bool ok = false;
                const double v = p.toDouble(&ok);
                if (!ok) {
                    appendLog(QStringLiteral("Exposure sequence invalid: Not a number: %1").arg(p));
                    return;
                }
                if (v < 0.1 || v > 5000.0) {
                    appendLog(QStringLiteral("Exposure sequence invalid: Out of range [0.1..5000]: %1").arg(v));
                    return;
                }
                seq.push_back(v);
            }
            if (seq.isEmpty()) {
                appendLog(QStringLiteral("Exposure sequence invalid: Empty sequence"));
                return;
            }
            m_sequenceExposureTimesMs = seq;
        });
    }

    if (ui.btnApplyRoi) {
        connect(ui.btnApplyRoi, &QPushButton::clicked, this, [this]() {
            m_options.spectrum_range_min = ui.spinRoiMin ? ui.spinRoiMin->value() : m_options.spectrum_range_min;
            m_options.spectrum_range_max = ui.spinRoiMax ? ui.spinRoiMax->value() : m_options.spectrum_range_max;
            ::saveOptions(m_optionsPath, m_options);
            tickSpectrum();
        });
    }

    if (ui.btnResetRoi) {
        connect(ui.btnResetRoi, &QPushButton::clicked, this, [this]() {
            m_options.spectrum_range_min = 0.0;
            m_options.spectrum_range_max = 2048.0;
            if (ui.spinRoiMin) ui.spinRoiMin->setValue(m_options.spectrum_range_min);
            if (ui.spinRoiMax) ui.spinRoiMax->setValue(m_options.spectrum_range_max);
            ::saveOptions(m_optionsPath, m_options);
            tickSpectrum();
        });
    }

    if (ui.spinRoiMin) {
        connect(ui.spinRoiMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            m_options.spectrum_range_min = ui.spinRoiMin ? ui.spinRoiMin->value() : m_options.spectrum_range_min;
            tickSpectrum();
        });
    }
    if (ui.spinRoiMax) {
        connect(ui.spinRoiMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            m_options.spectrum_range_max = ui.spinRoiMax ? ui.spinRoiMax->value() : m_options.spectrum_range_max;
            tickSpectrum();
        });
    }

    auto stepValue = [this]() -> int {
        if (ui.spinMotorStep) {
            return qMax(1, ui.spinMotorStep->value());
        }
        return qMax(1, m_options.step_x);
    };

    if (ui.btnMotorUp) {
        connect(ui.btnMotorUp, &QPushButton::clicked, this, [this, stepValue]() {
            QString err;
            move(0, -stepValue(), &err);
            if (!err.isEmpty()) appendLog(err);
        });
    }
    if (ui.btnMotorDown) {
        connect(ui.btnMotorDown, &QPushButton::clicked, this, [this, stepValue]() {
            QString err;
            move(0, stepValue(), &err);
            if (!err.isEmpty()) appendLog(err);
        });
    }
    if (ui.btnMotorLeft) {
        connect(ui.btnMotorLeft, &QPushButton::clicked, this, [this, stepValue]() {
            QString err;
            move(-stepValue(), 0, &err);
            if (!err.isEmpty()) appendLog(err);
        });
    }
    if (ui.btnMotorRight) {
        connect(ui.btnMotorRight, &QPushButton::clicked, this, [this, stepValue]() {
            QString err;
            move(stepValue(), 0, &err);
            if (!err.isEmpty()) appendLog(err);
        });
    }
    if (ui.btnMotorHome) {
        connect(ui.btnMotorHome, &QPushButton::clicked, this, [this]() {
            returnStageToSequenceStart();
        });
    }

    // Background worker thread
    // Start background file worker thread
    m_fileWorkerThread = new QThread(this);
    m_fileWorker = new QObject();
    m_fileWorker->moveToThread(m_fileWorkerThread);
    m_fileWorkerThread->start();

    setSequenceButtonsEnabled(false);
    refreshResults();
    startPixelink();
    setSequenceButtonsEnabled(m_sequenceRunning.load());
    updateConnectionStatusUi();

    QTimer::singleShot(0, this, [this]() {
        tickSpectrum();
    });
}

void Spektrometr::loop()
{
    updateLoop();
}

void Spektrometr::updateLoop()
{
    const bool spectrumTabActive = !ui.tabWidget || !ui.tabSpectrum || ui.tabWidget->currentWidget() == ui.tabSpectrum;
    const bool cameraActive = pixelinkOpen() || m_pixelinkConnectInProgress;
    const bool livePreviewNeeded = m_sequenceRunning.load() || (spectrumTabActive && cameraActive);
    const bool liveSpectrumNeeded = livePreviewNeeded;

    if (liveSpectrumNeeded) {
        tickSpectrum();
        tickPixelink();
    } else if (m_pixelinkTimer && m_pixelinkTimer->isActive()) {
        m_pixelinkTimer->stop();
    }
    tickPorts();
    setSequenceButtonsEnabled(m_sequenceRunning.load());
    updateConnectionStatusUi();
    QTimer::singleShot(livePreviewNeeded ? 250 : 1000, this, [this]() { updateLoop(); });
}

void Spektrometr::tickSpectrum()
{
    const bool spectrumTabActive = !ui.tabWidget || !ui.tabSpectrum || ui.tabWidget->currentWidget() == ui.tabSpectrum;
    if (!m_sequenceRunning.load() && !spectrumTabActive) {
        return;
    }

    if (!ui.labelSpectrumPlaceholder) {
        return;
    }

    if (m_spectrumWorkerRunning.exchange(true)) {
        m_spectrumRenderPending = true;
        return;
    }

    QImage frame;
    {
        QMutexLocker lock(&m_pixelinkFrameMutex);
        frame = m_pixelinkLatestFrame;
    }
    const QSize targetSize = ui.labelSpectrumPlaceholder->size().expandedTo(QSize(640, 260));
    const int roiMin = int(m_options.spectrum_range_min);
    const int roiMax = int(m_options.spectrum_range_max);
    const double calPixel1 = m_options.spectrum_cal_pixel1;
    const double calNm1 = m_options.spectrum_cal_nm1;
    const double calPixel2 = m_options.spectrum_cal_pixel2;
    const double calNm2 = m_options.spectrum_cal_nm2;
    const QPointer<Spektrometr> self(this);

    std::thread([self, frame = std::move(frame), targetSize, roiMin, roiMax, calPixel1, calNm1, calPixel2, calNm2]() mutable {
        QElapsedTimer workerTimer;
        workerTimer.start();
        auto wavelengthForPixel = [calPixel1, calNm1, calPixel2, calNm2](double pixel) -> double {
            const double dx = calPixel2 - calPixel1;
            if (qFuzzyIsNull(dx)) {
                return calNm1;
            }
            const double slope = (calNm2 - calNm1) / dx;
            const double intercept = calNm1 - slope * calPixel1;
            return (slope * pixel) + intercept;
        };

        auto buildSpectrumChart = [roiMin, roiMax, wavelengthForPixel](const QVector<double>& spectrum, const QSize& size, const QString& centerText) -> QImage {
            const int w = qMax(320, size.width());
            const int h = qMax(220, size.height());
            QImage chart(w, h, QImage::Format_ARGB32_Premultiplied);
            chart.fill(Qt::transparent);

            QPainter p(&chart);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::TextAntialiasing, true);

            QLinearGradient bg(0, 0, 0, h);
            bg.setColorAt(0.0, QColor(14, 14, 16));
            bg.setColorAt(1.0, QColor(8, 8, 10));
            p.fillRect(QRect(0, 0, w, h), bg);

            const QFont tickFont(QFont(p.font().family(), 8));
            const QFont titleFont(QFont(p.font().family(), 9, QFont::DemiBold));
            const QFontMetrics fmTicks(tickFont);
            const QFontMetrics fmTitle(titleFont);

            const int left = qMax(64, fmTicks.horizontalAdvance(QStringLiteral("0000.0")) + 18);
            const int right = qMax(18, fmTicks.horizontalAdvance(QStringLiteral("000")) / 2);
            const int top = qMax(12, fmTitle.height() + 8);
            const int bottom = qMax(54, fmTicks.height() * 2 + 18);
            const QRect plot(left, top, qMax(10, w - left - right), qMax(10, h - top - bottom));

            double minV = 0.0;
            double maxV = 1.0;
            if (!spectrum.isEmpty()) {
                minV = spectrum.first();
                maxV = spectrum.first();
                for (double v : spectrum) {
                    minV = qMin(minV, v);
                    maxV = qMax(maxV, v);
                }
                if (qFuzzyCompare(minV, maxV)) {
                    maxV = minV + 1.0;
                }
                const double pad = (maxV - minV) * 0.08;
                minV -= pad;
                maxV += pad;
            }

            {
                QLinearGradient g(plot.topLeft(), plot.bottomLeft());
                g.setColorAt(0.0, QColor(22, 22, 26));
                g.setColorAt(1.0, QColor(15, 15, 18));
                p.fillRect(plot, g);
            }

            const int majorX = 8;
            const int majorY = 5;
            const int minorX = majorX * 2;
            const int minorY = majorY * 2;

            p.setPen(QPen(QColor(35, 35, 40), 1));
            for (int i = 1; i < minorX; ++i) {
                const int x = plot.left() + (plot.width() * i) / minorX;
                p.drawLine(QPoint(x, plot.top()), QPoint(x, plot.bottom()));
            }
            for (int i = 1; i < minorY; ++i) {
                const int y = plot.top() + (plot.height() * i) / minorY;
                p.drawLine(QPoint(plot.left(), y), QPoint(plot.right(), y));
            }

            p.setPen(QPen(QColor(55, 55, 62), 1));
            for (int i = 1; i < majorX; ++i) {
                const int x = plot.left() + (plot.width() * i) / majorX;
                p.drawLine(QPoint(x, plot.top()), QPoint(x, plot.bottom()));
            }
            for (int i = 1; i < majorY; ++i) {
                const int y = plot.top() + (plot.height() * i) / majorY;
                p.drawLine(QPoint(plot.left(), y), QPoint(plot.right(), y));
            }

            p.setPen(QPen(QColor(165, 165, 175), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(plot.adjusted(0, 0, -1, -1), 4, 4);

            if (!spectrum.isEmpty()) {
                const double span = qMax(1.0, maxV - minV);
                QPolygonF curve;
                curve.reserve(spectrum.size());
                for (int i = 0; i < spectrum.size(); ++i) {
                    const double nx = (spectrum.size() <= 1) ? 0.0 : double(i) / double(spectrum.size() - 1);
                    const double ny = (spectrum[i] - minV) / span;
                    curve.push_back(QPointF(plot.left() + nx * plot.width(), plot.bottom() - ny * plot.height()));
                }

                QPainterPath areaPath;
                if (!curve.isEmpty()) {
                    areaPath.moveTo(QPointF(curve.first().x(), plot.bottom()));
                    areaPath.lineTo(curve.first());
                    for (int i = 1; i < curve.size(); ++i) areaPath.lineTo(curve[i]);
                    areaPath.lineTo(QPointF(curve.last().x(), plot.bottom()));
                    areaPath.closeSubpath();
                }
                QLinearGradient fill(plot.topLeft(), plot.bottomLeft());
                fill.setColorAt(0.0, QColor(0, 220, 120, 90));
                fill.setColorAt(1.0, QColor(0, 220, 120, 10));
                p.fillPath(areaPath, fill);

                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(0, 255, 180, 60), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPolyline(curve);
                p.setPen(QPen(QColor(0, 230, 150), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPolyline(curve);
            }

            p.setFont(tickFont);
            p.setPen(QColor(130, 130, 140));
            p.drawText(QRect(0, plot.top() - 6, left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxV, 'g', 4));
            p.drawText(QRect(0, plot.bottom() - (fmTicks.height() / 2), left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(minV, 'g', 4));

            {
                p.setPen(QColor(130, 130, 140));
                const int ticks = 5;
                for (int i = 0; i <= ticks; ++i) {
                    const double t = double(i) / double(ticks);
                    const double pixel = t * double(qMax(1, spectrum.size() - 1));
                    const double wavelength = wavelengthForPixel(pixel);
                    const int x = plot.left() + int(std::round(t * plot.width()));
                    p.drawLine(QPoint(x, plot.bottom()), QPoint(x, plot.bottom() + 4));

                    const QString s = QString::number(wavelength, 'f', 1);
                    const int tw = fmTicks.horizontalAdvance(s);
                    p.drawText(QRect(x - tw / 2, plot.bottom() + 6, tw + 2, fmTicks.height()), Qt::AlignHCenter | Qt::AlignTop, s);
                }
            }

            p.setPen(QColor(230, 230, 235));
            p.setFont(titleFont);
            p.drawText(QRect(8, 6, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Spectrum"));

            p.setFont(QFont(p.font().family(), 9));
            p.setPen(QColor(165, 165, 175));
            p.drawText(QRect(8, h - 22, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("ROI %1..%2").arg(roiMin).arg(roiMax));

            if (!centerText.isEmpty()) {
                p.setPen(QColor(180, 180, 190));
                p.drawText(plot, Qt::AlignCenter, centerText);
            }

            p.setPen(QColor(190, 190, 200));
            p.drawText(QRect(plot.left(), plot.bottom() + (fmTicks.height() * 2 + 12), plot.width(), fmTicks.height() + 2), Qt::AlignCenter, QStringLiteral("Wavelength [nm]"));
            p.save();
            p.translate(18, plot.top() + plot.height() / 2);
            p.rotate(-90);
            p.drawText(QRect(-plot.height() / 2, -12, plot.height(), 18), Qt::AlignCenter, QStringLiteral("Intensity"));
            p.restore();

            return chart;
        };

        const QVector<double> spectrum = frame.isNull() ? QVector<double>() : spectrumFromFrame(frame, roiMin, roiMax);
        const QString centerText = (frame.isNull() || spectrum.isEmpty()) ? QStringLiteral("Brak danych") : QString();
        const QImage chart = buildSpectrumChart(spectrum, targetSize, centerText);
        const qint64 workerMs = workerTimer.elapsed();

        if (!self || !self.data()) {
            return;
        }

        QMetaObject::invokeMethod(self.data(), [self, chart]() mutable {
            if (!self) {
                return;
            }

            const QPixmap pix = QPixmap::fromImage(chart);
            self->m_spectrumLatestImage = chart;
            self->m_spectrumLatestPixmap = pix;
            if (self->ui.labelSpectrumPlaceholder) {
                self->ui.labelSpectrumPlaceholder->setText(QString());
                self->ui.labelSpectrumPlaceholder->setPixmap(pix);
            }

            self->m_spectrumWorkerRunning = false;
            self->setSequenceButtonsEnabled(self->m_sequenceRunning.load());
            if (self->m_spectrumRenderPending.exchange(false)) {
                QTimer::singleShot(0, self.data(), [self]() {
                    if (self) {
                        self->tickSpectrum();
                    }
                });
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void Spektrometr::tickPixelink()
{
    // PixeLink preview.
    const bool spectrumTabActive = !ui.tabWidget || !ui.tabSpectrum || ui.tabWidget->currentWidget() == ui.tabSpectrum;
    if (!m_sequenceRunning.load() && !spectrumTabActive) {
        if (m_pixelinkTimer && m_pixelinkTimer->isActive()) {
            m_pixelinkTimer->stop();
        }
        return;
    }

    if (!pixelinkOpen() && !m_sequenceRunning.load()) {
        if (m_pixelinkTimer && m_pixelinkTimer->isActive()) {
            m_pixelinkTimer->stop();
        }
        return;
    }

    if (m_pixelinkTimer == nullptr) {
        m_pixelinkTimer = new QTimer(this);
        m_pixelinkTimer->setInterval(200);
        connect(m_pixelinkTimer, &QTimer::timeout, this, [this]() {
            if (m_pixelinkWorkerRunning.exchange(true)) {
                m_pixelinkRenderPending = true;
                return;
            }

            const QSize targetSize = ui.labelPixelinkPreview
                ? ui.labelPixelinkPreview->contentsRect().size().expandedTo(QSize(320, 240))
                : QSize(640, 480);
            const QPointer<Spektrometr> self(this);

            std::thread([self, targetSize]() mutable {
                if (!self) {
                    return;
                }

                QImage frame;
#if HAVE_PIXELINK_SDK
                if (self->m_pixelinkCamera != nullptr && self->m_pixelinkStreaming) {
                    F32 roi[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
                    U32 roiFlags = 0;
                    U32 roiCount = 4;
                    (void)PxLGetFeature(self->m_pixelinkCamera, FEATURE_ROI, &roiFlags, &roiCount, roi);

                    F32 pixelFormat = F32(PIXEL_FORMAT_MONO8);
                    U32 pixelFormatFlags = 0;
                    U32 pixelFormatCount = 1;
                    (void)PxLGetFeature(self->m_pixelinkCamera, FEATURE_PIXEL_FORMAT, &pixelFormatFlags, &pixelFormatCount, &pixelFormat);

                    const int width = qMax(1, int(std::lround(double(roi[2]))));
                    const int height = qMax(1, int(std::lround(double(roi[3]))));
                    int bytesPerPixel = 4;
                    const qint64 bufferSize64 = qint64(width) * qint64(height) * qint64(bytesPerPixel);
                    if (bufferSize64 > 0 && bufferSize64 <= std::numeric_limits<int>::max()) {
                        QByteArray rawFrame;
                        rawFrame.resize(int(bufferSize64));

                        FRAME_DESC frameDesc = {};
                        frameDesc.uSize = sizeof(frameDesc);
                        const PXL_RETURN_CODE frameRc = PxLGetNextFrame(self->m_pixelinkCamera, U32(rawFrame.size()), rawFrame.data(), &frameDesc);
                        if (API_SUCCESS(frameRc)) {
                            QByteArray grayFrame;
                            grayFrame.resize(width * height);
                            U32 graySize = U32(grayFrame.size());
                            const PXL_RETURN_CODE formatRc = PxLFormatImage(rawFrame.constData(), &frameDesc, PIXEL_FORMAT_MONO8, grayFrame.data(), &graySize);
                            if (API_SUCCESS(formatRc) && graySize > 0) {
                                QImage image(reinterpret_cast<const uchar*>(grayFrame.constData()), width, height, width, QImage::Format_Grayscale8);
                                frame = image.copy();
                            } else if (U32(pixelFormat) == PIXEL_FORMAT_MONO8) {
                                QImage image(reinterpret_cast<const uchar*>(rawFrame.constData()), width, height, width, QImage::Format_Grayscale8);
                                frame = image.copy();
                            }
                        }
                    }
                }
#endif

                if (frame.isNull()) {
                    QMutexLocker lock(&self->m_pixelinkFrameMutex);
                    frame = self->m_pixelinkLatestFrame;
                }

                QImage rendered;
                QString centerText;
                if (frame.isNull()) {
                    centerText = QStringLiteral("PixeLink preview\nNo frame");
                } else {
                    {
                        QMutexLocker lock(&self->m_pixelinkFrameMutex);
                        self->m_pixelinkLatestFrame = frame;
                    }
                    self->m_pixelinkLatestFrameSeq.fetch_add(1);

                    rendered = frame;
#if SPEKTROMETR_HAS_OPENCV
                    const cv::Mat src = qImageToCvMat(frame);
                    if (!src.empty()) {
                        try {
                            cv::Mat dst;
                            cv::resize(src, dst, cv::Size(qMax(1, targetSize.width()), qMax(1, targetSize.height())), 0.0, 0.0, cv::INTER_CUBIC);
                            const QImage out = cvMatToQImage(dst);
                            if (!out.isNull()) {
                                rendered = out;
                            }
                        } catch (const cv::Exception&) {
                            rendered = frame.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        }
                    }
#else
                    rendered = frame.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
#endif
                }

                QMetaObject::invokeMethod(self.data(), [self, rendered, targetSize, centerText]() mutable {
                    if (!self) {
                        return;
                    }

                    if (self->ui.labelPixelinkPreview) {
                        if (rendered.isNull()) {
                            self->ui.labelPixelinkPreview->setText(QString());
                            self->ui.labelPixelinkPreview->setPixmap(QPixmap::fromImage(renderCenteredTextImage(targetSize, centerText, QColor(220, 220, 220), QColor(16, 17, 20))));
                        } else {
                            self->ui.labelPixelinkPreview->setText(QString());
                            self->ui.labelPixelinkPreview->setPixmap(QPixmap::fromImage(rendered));
                        }
                    }

                    self->tickSpectrum();

                    self->m_pixelinkWorkerRunning = false;
                    self->setSequenceButtonsEnabled(self->m_sequenceRunning.load());
                    self->updateConnectionStatusUi();

                    if (self->m_pixelinkRenderPending.exchange(false)) {
                        QTimer::singleShot(0, self.data(), [self]() {
                            if (self) {
                                self->tickPixelink();
                            }
                        });
                    }
                }, Qt::QueuedConnection);
            }).detach();
        });
    }
    if (!m_pixelinkTimer->isActive()) {
        m_pixelinkTimer->start();
    }

#if HAVE_PIXELINK_SDK
    if (m_pixelinkReconnectTimer == nullptr) {
        m_pixelinkReconnectTimer = new QTimer(this);
        m_pixelinkReconnectTimer->setInterval(2500);
        connect(m_pixelinkReconnectTimer, &QTimer::timeout, this, [this]() {
            if (m_pixelinkCamera == nullptr || !m_pixelinkStreaming) {
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (nowMs >= m_pixelinkNextConnectAllowedMs) {
                    startPixelink();
                }
            }
        });
    }
    if (!m_pixelinkReconnectTimer->isActive()) {
        m_pixelinkReconnectTimer->start();
    }
#endif

    setSequenceButtonsEnabled(m_sequenceRunning.load());
}

void Spektrometr::tickPorts()
{
    // Periodically refresh serial port lists to detect plug/unplug.
    if (m_portTimer == nullptr) {
        m_portTimer = new QTimer(this);
        m_portTimer->setInterval(1500);
        connect(m_portTimer, &QTimer::timeout, this, [this]() {
            refreshPortLists();
#ifdef Q_OS_WIN
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (m_portX == nullptr || !m_portX->isOpen() || m_portY == nullptr || !m_portY->isOpen()) {
                if (nowMs >= m_portsNextConnectAllowedMs) {
                    const QString wantX = ui.comboPortX ? normalizedSerialPortName(ui.comboPortX->currentText()) : normalizedSerialPortName(m_options.port_x);
                    const QString wantY = ui.comboPortY ? normalizedSerialPortName(ui.comboPortY->currentText()) : normalizedSerialPortName(m_options.port_y);
                    openSerialPort(m_portX, m_openPortX, wantX, "X");
                    openSerialPort(m_portY, m_openPortY, wantY, "Y");
                }
            }
#endif
            setSequenceButtonsEnabled(m_sequenceRunning.load());
        });
    }
    if (!m_portTimer->isActive()) {
        m_portTimer->start();
    }
}

void Spektrometr::appendLog(const QString& line)
{
    if (line.isEmpty()) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, line]() { appendLog(line); }, Qt::QueuedConnection);
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (line == m_lastToastMessage && (nowMs - m_lastToastMs) < 350) {
        return;
    }
    m_lastToastMessage = line;
    m_lastToastMs = nowMs;

    auto* cw = centralWidget();
    if (!cw) {
        return;
    }

    if (!m_toastHost) {
        m_toastHost = new QWidget(cw);
        m_toastHost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_toastHost->setAttribute(Qt::WA_StyledBackground, true);
        m_toastHost->setStyleSheet(QStringLiteral("background:transparent;"));
        m_toastLayout = new QVBoxLayout(m_toastHost);
        m_toastLayout->setContentsMargins(12, 12, 12, 12);
        m_toastLayout->setSpacing(8);
    }

    m_toastHost->setGeometry(cw->rect());
    m_toastHost->raise();

    auto* host = m_toastHost ? m_toastHost : cw;
    if (!host) {
        return;
    }

    auto* toast = new QFrame(host);
    toast->setObjectName(QStringLiteral("toastBubble"));
    toast->setFrameShape(QFrame::StyledPanel);
    toast->setStyleSheet(QStringLiteral(
        "QFrame#toastBubble{background:rgba(30,30,36,230);border:1px solid rgba(255,255,255,40);border-radius:8px;}"
        "QLabel{color:#f0f0f0;}"
    ));
    toast->setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(toast);
    layout->setContentsMargins(12, 8, 12, 8);
    auto* label = new QLabel(line, toast);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    layout->addWidget(label);

    toast->setFixedWidth(360);
    toast->adjustSize();
    if (m_toastLayout) {
        m_toastLayout->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
        m_toastLayout->addWidget(toast, 0, Qt::AlignLeft);
    }
    toast->show();
    toast->raise();
    QTimer::singleShot(3000, toast, &QObject::deleteLater);
}

void Spektrometr::pauseSequence(const QString& reason)
{
    if (QThread::currentThread() != thread()) {
        m_sequencePaused.store(true);
        QMetaObject::invokeMethod(this, [this, reason]() { pauseSequence(reason); }, Qt::BlockingQueuedConnection);
        return;
    }

    if (!m_sequenceRunning.load()) {
        return;
    }

    m_sequencePaused.store(true);

    // Keep Stop enabled while paused.
    setSequenceButtonsEnabled(true);
    if (ui.btnStopSequence) ui.btnStopSequence->setEnabled(true);
    appendLog(QStringLiteral("Sequence paused: %1").arg(reason));

    showSequencePauseDialog(reason);

    if (m_sequenceReconnectTimer == nullptr) {
        m_sequenceReconnectTimer = new QTimer(this);
        m_sequenceReconnectTimer->setInterval(800);
        connect(m_sequenceReconnectTimer, &QTimer::timeout, this, &Spektrometr::tryAutoResumeSequence);
    }
    if (!m_sequenceReconnectTimer->isActive()) {
        m_sequenceReconnectTimer->start();
    }
}

void Spektrometr::resumeSequence()
{
    if (m_sequenceReconnectTimer) m_sequenceReconnectTimer->stop();
    if (!m_sequenceRunning.load()) {
        return;
    }

    m_sequencePaused.store(false);
}

void Spektrometr::showSequencePauseDialog(const QString& reason)
{
    if (reason.isEmpty()) {
        m_pauseDialogOpen = false;
        return;
    }

    if (m_pauseDialogOpen) {
        return;
    }
    m_pauseDialogOpen = true;

    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setWindowTitle(tr("Sequence paused"));
    dlg->setModal(true);
    dlg->setMinimumWidth(520);

    auto* root = new QVBoxLayout(dlg);
    auto* title = new QLabel(tr("Sequence paused"), dlg);
    QFont f = title->font();
    f.setPointSize(qMax(10, f.pointSize() + 1));
    f.setBold(true);
    title->setFont(f);
    root->addWidget(title);

    auto* reasonLbl = new QLabel(tr("Reason: %1").arg(reason), dlg);
    reasonLbl->setWordWrap(true);
    root->addWidget(reasonLbl);

    const bool pixelinkConnected = pixelinkOpen() || m_pixelinkStreaming || !m_pixelinkLatestFrame.isNull();
    const QString pixelinkMsg = pixelinkConnected ? QStringLiteral("connected") : QStringLiteral("disconnected");
    const QString motorsMsg = portsOpen() ? QStringLiteral("connected") : QStringLiteral("disconnected");
    auto* statusLbl = new QLabel(QStringLiteral("Status:\nPixeLINK: %1\nMotors: %2").arg(pixelinkMsg).arg(motorsMsg), dlg);
    statusLbl->setWordWrap(true);
    root->addWidget(statusLbl);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::NoButton, dlg);
    auto* btnContinue = new QPushButton(tr("Continue"), dlg);
    auto* btnAbort = new QPushButton(tr("Abort"), dlg);
    buttons->addButton(btnContinue, QDialogButtonBox::AcceptRole);
    buttons->addButton(btnAbort, QDialogButtonBox::DestructiveRole);
    root->addWidget(buttons);

    connect(btnContinue, &QPushButton::clicked, this, [this, dlg]() {
        if (m_sequenceReconnectTimer && !m_sequenceReconnectTimer->isActive()) {
            m_sequenceReconnectTimer->start();
        }
        dlg->close();
    });
    connect(btnAbort, &QPushButton::clicked, this, [this, dlg]() {
        dlg->close();
        stopSequence();
    });
    connect(dlg, &QDialog::finished, this, [this](int) {
        m_pauseDialogOpen = false;
    });

    dlg->show();
}

void Spektrometr::runSequenceLoop(SequenceRunSnapshot snapshot)
{
    if (!m_sequenceRunning.load()) {
        return;
    }

    if (m_movementMap.isEmpty()) {
        pauseSequence(QStringLiteral("No sequence plan"));
        return;
    }

    if (m_sequencePointIndex < 0) {
        m_sequencePointIndex = 0;
    }

    const int roiMin = snapshot.roiMin;
    const int roiMax = snapshot.roiMax;
    const QVector<double> exposures = snapshot.exposures;
    if (exposures.isEmpty()) {
        pauseSequence(QStringLiteral("Exposure sequence missing"));
        return;
    }

    while (m_sequenceRunning.load() && m_sequencePointIndex < m_movementMap.size()) {
        if (!waitForSequenceRunning()) {
            return;
        }

        QString hardwareReason;
        if (!check_hardware(&hardwareReason)) {
            pauseSequence(hardwareReason.isEmpty() ? QStringLiteral("Hardware not ready") : hardwareReason);
            if (!waitForSequenceRunning()) {
                return;
            }
            continue;
        }

        const SequencePlanPoint pt = m_movementMap.at(m_sequencePointIndex);
        const int dxUm = pt.xUm - m_stageOffsetXUm.load();
        const int dyUm = pt.yUm - m_stageOffsetYUm.load();
        QString moveErr;
        if ((dxUm != 0 || dyUm != 0) && !move(dxUm, dyUm, &moveErr)) {
            pauseSequence(moveErr);
            if (!waitForSequenceRunning()) {
                return;
            }
            continue;
        }

        bool pointNeedsRetry = false;
        int completedExposureCount = 0;
        for (int expIdx = 0; expIdx < exposures.size() && m_sequenceRunning.load(); ++expIdx) {
            if (!waitForSequenceRunning()) {
                return;
            }

            if (!check_hardware(&hardwareReason)) {
                pauseSequence(hardwareReason.isEmpty() ? QStringLiteral("Hardware not ready") : hardwareReason);
                if (!waitForSequenceRunning()) {
                    return;
                }
                pointNeedsRetry = true;
                break;
            }

            const double expMs = exposures[expIdx];
            appendLog(QStringLiteral("Point %1/%2: waiting for camera frame at %3 ms (exposure %4/%5)")
                .arg(m_sequencePointIndex + 1)
                .arg(m_movementMap.size())
                .arg(QString::number(expMs, 'f', 1))
                .arg(expIdx + 1)
                .arg(exposures.size()));

            QString expErr;
#if HAVE_PIXELINK_SDK
            if (!setPixelinkExposure(m_pixelinkCamera, expMs, &expErr) && !expErr.isEmpty()) {
                pauseSequence(QStringLiteral("Exposure change failed: %1").arg(expErr));
                if (!waitForSequenceRunning()) {
                    return;
                }
                pointNeedsRetry = true;
                break;
            }
            if (!waitForPixelinkExposureApplied(m_pixelinkCamera, expMs, &expErr) && !expErr.isEmpty()) {
                pauseSequence(QStringLiteral("Exposure did not settle: %1").arg(expErr));
                if (!waitForSequenceRunning()) {
                    return;
                }
                pointNeedsRetry = true;
                break;
            }
#endif

            QImage frame;
            const quint64 prevSeq = m_pixelinkLatestFrameSeq.load();
            QElapsedTimer waitTimer;
            waitTimer.start();
            bool frameReady = false;
            while (m_sequenceRunning.load() && !QThread::currentThread()->isInterruptionRequested()) {
                if (!portsOpen() || !pixelinkOpen()) {
                    pauseSequence(QStringLiteral("Connection lost"));
                    if (!waitForSequenceRunning()) {
                        return;
                    }
                    pointNeedsRetry = true;
                    break;
                }

                if (!m_sequencePaused.load() && m_pixelinkLatestFrameSeq.load() != prevSeq) {
                    QMutexLocker lock(&m_pixelinkFrameMutex);
                    frame = m_pixelinkLatestFrame;
                    if (!frame.isNull()) {
                        frameReady = true;
                        break;
                    }
                }

                if (m_sequencePaused.load()) {
                    if (!waitForSequenceRunning()) {
                        return;
                    }
                    continue;
                }

                if (waitTimer.elapsed() > 5000) {
                    pauseSequence(QStringLiteral("PixeLink frame timeout"));
                    if (!waitForSequenceRunning()) {
                        return;
                    }
                    pointNeedsRetry = true;
                    break;
                }

                QThread::msleep(10);
            }

            if (!m_sequenceRunning.load() || m_sequencePaused.load()) {
                return;
            }

            if (pointNeedsRetry || !frameReady || frame.isNull()) {
                pointNeedsRetry = true;
                break;
            }

            const QVector<double> spectrum = spectrumFromFrame(frame, roiMin, roiMax);
            const QString perPath = QDir(m_sequenceSessionFolder).absoluteFilePath(QStringLiteral("pt_x%1_y%2_e%3.csv").arg(pt.ix).arg(pt.iy).arg(expIdx + 1));
            QFile f(perPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                pauseSequence(QStringLiteral("Failed to open exposure file: %1").arg(perPath));
                if (!waitForSequenceRunning()) {
                    return;
                }
                pointNeedsRetry = true;
                break;
            } else {
                QTextStream out(&f);
                out << "lambda_idx" << ",t_" << QString::number(expMs, 'f', 1) << "_ms\n";
                for (int i = 0; i < spectrum.size(); ++i) {
                    out << i << "," << QString::number(spectrum[i], 'f', 3) << "\n";
                }
                ++completedExposureCount;
            }
        }

        if (pointNeedsRetry) {
            continue;
        }

        if (completedExposureCount < exposures.size()) {
            if (!m_sequenceRunning.load() || m_sequencePaused.load()) {
                return;
            }
            continue;
        }

        QString errf;
        if (!save_spec_from_files(pt, exposures, &errf)) {
            pauseSequence(errf.isEmpty() ? QStringLiteral("Failed to save spectra") : errf);
            if (!waitForSequenceRunning()) {
                return;
            }
            continue;
        }

        ++m_sequencePointIndex;

        if (!m_sequenceRunning.load() || m_sequencePaused.load()) {
            return;
        }
    }

    if (!m_sequenceRunning.load() || m_sequencePaused.load()) {
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        finalizeSequenceSuccess();
    }, Qt::BlockingQueuedConnection);
}

void Spektrometr::ensureLoadingOverlay()
{
    if (m_loadingOverlay) {
        return;
    }

    auto* cw = centralWidget();
    if (!cw) {
        return;
    }

    m_loadingOverlay = new QWidget(cw);
    m_loadingOverlay->setAttribute(Qt::WA_StyledBackground, true);
    m_loadingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_loadingOverlay->setStyleSheet(QStringLiteral("QWidget{background:rgba(0,0,0,150);} QLabel{color:white;font-size:16px;}"));

    auto* layout = new QVBoxLayout(m_loadingOverlay);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addStretch();

    m_loadingLabel = new QLabel(tr("Loading..."), m_loadingOverlay);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_loadingLabel);
    layout->addStretch();

    m_loadingOverlay->hide();
}

void Spektrometr::loadOptionsToUi()
{
    if (ui.spinScanWidth) {
        QSignalBlocker b(ui.spinScanWidth);
        ui.spinScanWidth->setValue(m_options.width);
    }
    if (ui.spinScanHeight) {
        QSignalBlocker b(ui.spinScanHeight);
        ui.spinScanHeight->setValue(m_options.height);
    }
    if (ui.spinStepX) {
        QSignalBlocker b(ui.spinStepX);
        ui.spinStepX->setValue(m_options.step_x);
    }
    if (ui.spinStepY) {
        QSignalBlocker b(ui.spinStepY);
        ui.spinStepY->setValue(m_options.step_y);
    }

    if (ui.spinRoiMin) {
        QSignalBlocker b(ui.spinRoiMin);
        ui.spinRoiMin->setValue(m_options.spectrum_range_min);
    }
    if (ui.spinRoiMax) {
        QSignalBlocker b(ui.spinRoiMax);
        ui.spinRoiMax->setValue(m_options.spectrum_range_max);
    }
    if (ui.spinExposureSpectrum) {
        QSignalBlocker b(ui.spinExposureSpectrum);
        ui.spinExposureSpectrum->setValue(m_options.exposure_time_ms);
    }
    if (ui.spinGainSpectrum) {
        QSignalBlocker b(ui.spinGainSpectrum);
        ui.spinGainSpectrum->setValue(m_options.gain);
    }

    if (ui.comboPortX) {
        QSignalBlocker b(ui.comboPortX);
        const int ix = ui.comboPortX->findText(m_options.port_x);
        if (ix >= 0) {
            ui.comboPortX->setCurrentIndex(ix);
        } else if (ui.comboPortX->count() > 0) {
            ui.comboPortX->setCurrentIndex(0);
        }
    }
    if (ui.comboPortY) {
        QSignalBlocker b(ui.comboPortY);
        const int ix = ui.comboPortY->findText(m_options.port_y);
        if (ix >= 0) {
            ui.comboPortY->setCurrentIndex(ix);
        } else if (ui.comboPortY->count() > 0) {
            ui.comboPortY->setCurrentIndex(0);
        }
    }

    if (auto* edit = findChild<QLineEdit*>(QStringLiteral("editExposureSequence"))) {
        QSignalBlocker b(edit);
        edit->setText(m_options.sequence_exposure_times);
    }

    m_sequenceExposureTimesMs.clear();
    const auto parts = m_options.sequence_exposure_times.split(';', Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        bool ok = false;
        const double v = part.toDouble(&ok);
        if (ok && v > 0.0) {
            m_sequenceExposureTimesMs.push_back(v);
        }
    }
}

bool Spektrometr::hasSpectrumCalibration() const
{
    return !qFuzzyCompare(m_options.spectrum_cal_pixel1 + 1.0, m_options.spectrum_cal_pixel2 + 1.0);
}

double Spektrometr::wavelengthForPixel(double pixel) const
{
    const double dx = m_options.spectrum_cal_pixel2 - m_options.spectrum_cal_pixel1;
    if (qFuzzyIsNull(dx)) {
        return m_options.spectrum_cal_nm1;
    }
    const double slope = (m_options.spectrum_cal_nm2 - m_options.spectrum_cal_nm1) / dx;
    const double intercept = m_options.spectrum_cal_nm1 - slope * m_options.spectrum_cal_pixel1;
    return (slope * pixel) + intercept;
}

void Spektrometr::setSpectrumCalibration(double pixel1, double nm1, double pixel2, double nm2)
{
    m_options.spectrum_cal_pixel1 = pixel1;
    m_options.spectrum_cal_nm1 = nm1;
    m_options.spectrum_cal_pixel2 = pixel2;
    m_options.spectrum_cal_nm2 = nm2;
    ::saveOptions(m_optionsPath, m_options);
    tickSpectrum();
}

void Spektrometr::showSpectrumCalibrationDialog()
{
    runCalibrationMode(0);
}

void Spektrometr::showBrightnessCalibrationDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Brightness calibration"));
    dlg.setModal(true);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Use this mode to calibrate brightness gradient across the diagonal stripes."), &dlg));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() { dlg.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

void Spektrometr::showFunctionCalibrationDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Function calibration"));
    dlg.setModal(true);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Use this mode to define the function that describes the spectrometer geometry later."), &dlg));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() { dlg.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

void Spektrometr::runCalibrationMode(int modeIndex)
{
    if (modeIndex == 0) {
        showBrightnessCalibrationDialog();
        return;
    }
    if (modeIndex == 1) {
        showFunctionCalibrationDialog();
        return;
    }
}

void Spektrometr::saveOptions()
{
    ::saveOptions(m_optionsPath, m_options);
}

void Spektrometr::refreshPortLists()
{
#ifdef Q_OS_WIN
    const auto ports = QSerialPortInfo::availablePorts();
    const QString currentX = ui.comboPortX ? ui.comboPortX->currentText() : m_options.port_x;
    const QString currentY = ui.comboPortY ? ui.comboPortY->currentText() : m_options.port_y;

    auto fillCombo = [](QComboBox* combo, const QString& selected, const QList<QSerialPortInfo>& /*unused*/, const QVector<QString>& names) {
        if (!combo) return;
        QSignalBlocker b(combo);
        combo->clear();
        for (const auto& name : names) {
            combo->addItem(name);
        }
        const int ix = combo->findText(selected);
        if (ix >= 0) {
            combo->setCurrentIndex(ix);
        } else if (combo->count() > 0) {
            combo->setCurrentIndex(0);
        }
    };

    QVector<QString> names;
    names.reserve(ports.size());
    for (const auto& port : ports) {
        names.push_back(port.portName());
    }
    if (names.isEmpty()) {
        names.push_back(QStringLiteral("(none)"));
    }
    fillCombo(ui.comboPortX, currentX.isEmpty() ? m_options.port_x : currentX, ports, names);
    fillCombo(ui.comboPortY, currentY.isEmpty() ? m_options.port_y : currentY, ports, names);
#else
    if (ui.comboPortX) {
        QSignalBlocker b(ui.comboPortX);
        ui.comboPortX->clear();
    }
    if (ui.comboPortY) {
        QSignalBlocker b(ui.comboPortY);
        ui.comboPortY->clear();
    }
#endif
    updateConnectionStatusUi();
}

void Spektrometr::closeMotorPorts()
{
#ifdef Q_OS_WIN
    if (m_portX) {
        if (m_portX->isOpen()) m_portX->close();
        m_portX->deleteLater();
        m_portX = nullptr;
    }
    if (m_portY) {
        if (m_portY->isOpen()) m_portY->close();
        m_portY->deleteLater();
        m_portY = nullptr;
    }
    m_openPortX.clear();
    m_openPortY.clear();
    m_portsNotConnectedLogged = false;
#endif
    updateConnectionStatusUi();
}

void Spektrometr::ensureConnectionStatusLabel()
{
    auto* bar = statusBar();
    if (!bar || m_connectionStatusLabel) {
        return;
    }

    m_connectionStatusLabel = new QLabel(bar);
    m_connectionStatusLabel->setTextFormat(Qt::RichText);
    m_connectionStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_connectionStatusLabel->setContentsMargins(6, 2, 6, 2);
    m_connectionStatusLabel->setMinimumWidth(640);
    m_connectionStatusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    bar->addPermanentWidget(m_connectionStatusLabel, 1);
}

void Spektrometr::updateConnectionStatusUi()
{
    ensureConnectionStatusLabel();
    if (!m_connectionStatusLabel) {
        return;
    }

    const bool pixelinkConnected = pixelinkOpen();
    const QString selectedX = normalizedSerialPortName(ui.comboPortX ? ui.comboPortX->currentText() : m_options.port_x);
    const QString selectedY = normalizedSerialPortName(ui.comboPortY ? ui.comboPortY->currentText() : m_options.port_y);
    const QString portX = m_openPortX.isEmpty() ? selectedX : m_openPortX;
    const QString portY = m_openPortY.isEmpty() ? selectedY : m_openPortY;
    const bool portXConnected = (m_portX && m_portX->isOpen());
    const bool portYConnected = (m_portY && m_portY->isOpen());
    const bool retryPending = !pixelinkConnected && !m_pixelinkConnectInProgress && m_pixelinkNextConnectAllowedMs > QDateTime::currentMSecsSinceEpoch();

    const QString portXText = formatPortStatus(QStringLiteral("X"), portX, portXConnected);
    const QString portYText = formatPortStatus(QStringLiteral("Y"), portY, portYConnected);
    const QString pixelinkText = formatPixelinkStatus(m_pixelinkConnectInProgress, pixelinkConnected, retryPending);

    const QString portXColor = portXConnected ? QStringLiteral("#86d98a") : QStringLiteral("#e07a7a");
    const QString portYColor = portYConnected ? QStringLiteral("#86d98a") : QStringLiteral("#e07a7a");
    const QString pixelinkColor = pixelinkConnected ? QStringLiteral("#86d98a") : ((m_pixelinkConnectInProgress || retryPending) ? QStringLiteral("#e0c46a") : QStringLiteral("#e07a7a"));

    const QString statusText = QStringLiteral("<span style='color:%1'>%2</span> | <span style='color:%3'>%4</span> | <span style='color:%5'>%6</span>")
        .arg(portXColor, portXText.toHtmlEscaped(), portYColor, portYText.toHtmlEscaped(), pixelinkColor, pixelinkText.toHtmlEscaped());
    const QString statusToolTip = QStringLiteral("X selected: %1\nX state: %2\nY selected: %3\nY state: %4\nCamera: %5")
        .arg(selectedX.isEmpty() ? QStringLiteral("n/a") : selectedX,
             portXConnected ? QStringLiteral("open") : QStringLiteral("closed"),
             selectedY.isEmpty() ? QStringLiteral("n/a") : selectedY,
             portYConnected ? QStringLiteral("open") : QStringLiteral("closed"),
             pixelinkText);

    if (m_connectionStatusLabel->text() == statusText && m_connectionStatusLabel->toolTip() == statusToolTip) {
        return;
    }

    m_connectionStatusLabel->setText(statusText);
    m_connectionStatusLabel->setToolTip(statusToolTip);
}

void Spektrometr::setSequenceButtonsEnabled(bool running)
{
    QString reason;
    const bool hardwareReady = check_hardware(&reason);
    const bool busy = running || (m_sequenceWorkerThread && m_sequenceWorkerThread->isRunning());
    const bool startEnabled = !busy && hardwareReady;

    if (ui.btnStartSequence) ui.btnStartSequence->setEnabled(startEnabled);
    if (ui.btnStopSequence) ui.btnStopSequence->setEnabled(busy || m_sequencePaused.load());
    if (ui.btnStartSequence && !startEnabled && !reason.isEmpty()) {
        ui.btnStartSequence->setToolTip(reason);
    } else if (ui.btnStartSequence) {
        ui.btnStartSequence->setToolTip(QString());
    }

}

void Spektrometr::returnStageToSequenceStart()
{
    if (!m_stageHasReference) {
        m_stageOffsetXUm.store(0);
        m_stageOffsetYUm.store(0);
        return;
    }

    const int dx = -m_stageOffsetXUm.load();
    const int dy = -m_stageOffsetYUm.load();
    if (dx == 0 && dy == 0) {
        return;
    }

    QString err;
    if (!move(dx, dy, &err)) {
        if (!err.isEmpty()) {
            appendLog(QStringLiteral("Failed to return stage to sequence start: %1").arg(err));
        }
        return;
    }

    m_stageOffsetXUm.store(0);
    m_stageOffsetYUm.store(0);
}

QVector<double> Spektrometr::spectrumFromFrame(const QImage& src, int roiMin, int roiMax)
{
    if (src.isNull()) {
        return {};
    }

    const QImage img = src.convertToFormat(QImage::Format_Grayscale8);
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0) {
        return {};
    }

    int y1 = qBound(0, qMin(roiMin, roiMax), h - 1);
    int y2 = qBound(0, qMax(roiMin, roiMax), h - 1);
    if (y2 < y1) qSwap(y1, y2);

    QVector<double> spec;
    spec.resize(w);
    const int rows = qMax(1, y2 - y1 + 1);

#if SPEKTROMETR_HAS_OPENCV
    cv::Mat mat(h, w, CV_8UC1, const_cast<uchar*>(img.bits()), img.bytesPerLine());
    cv::Mat roi = mat.rowRange(y1, y2 + 1);
    cv::Mat colSum;
    cv::reduce(roi, colSum, 0, cv::REDUCE_SUM, CV_64F);
    if (!colSum.empty()) {
        const double* sums = colSum.ptr<double>(0);
        for (int x = 0; x < w; ++x) {
            spec[x] = sums[x] / double(rows);
        }
        return spec;
    }
#endif

    for (int x = 0; x < w; ++x) {
        double sum = 0.0;
        for (int y = y1; y <= y2; ++y) {
            sum += qGray(img.pixel(x, y));
        }
        spec[x] = sum / double(rows);
    }

    return spec;
}

void Spektrometr::startPixelink()
{
#if HAVE_PIXELINK_SDK
    if (m_pixelinkCamera != nullptr && m_pixelinkStreaming) {
        return;
    }

    if (m_pixelinkConnectInProgress) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs < m_pixelinkNextConnectAllowedMs) {
        return;
    }

    m_pixelinkConnectInProgress = true;

    U32 cameraCount = 0;
    const PXL_RETURN_CODE countRc = PxLGetNumberCameras(nullptr, &cameraCount);
    if (!API_SUCCESS(countRc) || cameraCount == 0) {
        m_pixelinkConnectInProgress = false;
        m_pixelinkNextConnectAllowedMs = nowMs + 3000;
        if (!m_pixelinkNotConnectedLogged) {
            appendLog(QStringLiteral("PixeLink camera not found"));
            m_pixelinkNotConnectedLogged = true;
        }
        updateConnectionStatusUi();
        return;
    }

    QVector<U32> serials;
    serials.resize(int(cameraCount));
    U32 serialCount = cameraCount;
    const PXL_RETURN_CODE listRc = PxLGetNumberCameras(serials.data(), &serialCount);
    if (!API_SUCCESS(listRc) || serialCount == 0) {
        m_pixelinkConnectInProgress = false;
        m_pixelinkNextConnectAllowedMs = nowMs + 3000;
        if (!m_pixelinkNotConnectedLogged) {
            appendLog(QStringLiteral("PixeLink camera enumeration failed (0x%1)").arg(QString::number(listRc, 16)));
            m_pixelinkNotConnectedLogged = true;
        }
        updateConnectionStatusUi();
        return;
    }

    const int cameraIndex = qBound(0, m_options.camera_index, int(serialCount) - 1);
    const U32 serialNumber = serials.at(cameraIndex);

    HANDLE camera = nullptr;
    const PXL_RETURN_CODE initRc = PxLInitialize(serialNumber, &camera);
    if (!API_SUCCESS(initRc) || camera == nullptr) {
        m_pixelinkConnectInProgress = false;
        m_pixelinkNextConnectAllowedMs = nowMs + 3000;
        if (!m_pixelinkNotConnectedLogged) {
            appendLog(QStringLiteral("PxLInitialize failed for camera %1 (0x%2)").arg(serialNumber).arg(QString::number(initRc, 16)));
            m_pixelinkNotConnectedLogged = true;
        }
        updateConnectionStatusUi();
        return;
    }

    const PXL_RETURN_CODE streamRc = PxLSetStreamState(camera, START_STREAM);
    if (!API_SUCCESS(streamRc)) {
        PxLUninitialize(camera);
        m_pixelinkConnectInProgress = false;
        m_pixelinkNextConnectAllowedMs = nowMs + 3000;
        if (!m_pixelinkNotConnectedLogged) {
            appendLog(QStringLiteral("PxLSetStreamState(START_STREAM) failed (0x%1)").arg(QString::number(streamRc, 16)));
            m_pixelinkNotConnectedLogged = true;
        }
        updateConnectionStatusUi();
        return;
    }

    m_pixelinkCamera = camera;
    m_pixelinkStreaming = true;
    m_pixelinkConnectInProgress = false;
    m_pixelinkNextConnectAllowedMs = 0;
    m_pixelinkNotConnectedLogged = false;
    appendLog(QStringLiteral("PixeLink preview started (serial %1)").arg(serialNumber));
            setSequenceButtonsEnabled(m_sequenceRunning.load());
    updateConnectionStatusUi();
#else
#endif
}

void Spektrometr::stopPixelink()
{
#if HAVE_PIXELINK_SDK
    m_pixelinkNextConnectAllowedMs = 0;
    m_pixelinkConnectInProgress = false;
    m_pixelinkNotConnectedLogged = false;

    if (m_pixelinkCamera != nullptr) {
        if (m_pixelinkStreaming) {
            PxLSetStreamState(m_pixelinkCamera, STOP_STREAM);
        }
        PxLUninitialize(m_pixelinkCamera);
        m_pixelinkCamera = nullptr;
    }
    m_pixelinkStreaming = false;
    setSequenceButtonsEnabled(false);
    updateConnectionStatusUi();
#endif
}

Spektrometr::~Spektrometr()
{
    stopSequence();
    if (m_sequenceWorkerThread) {
        m_sequenceWorkerThread->requestInterruption();
        while (m_sequenceWorkerThread && m_sequenceWorkerThread->isRunning()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    stopPixelink();
    closeMotorPorts();
    if (m_pixelinkTimer) m_pixelinkTimer->stop();
    if (m_pixelinkReconnectTimer) m_pixelinkReconnectTimer->stop();
    if (m_portTimer) m_portTimer->stop();
    if (m_sequenceReconnectTimer) m_sequenceReconnectTimer->stop();

    if (m_fileWorkerThread) {
        m_fileWorkerThread->wait();
    }
    if (m_fileWorker) {
        delete m_fileWorker;
        m_fileWorker = nullptr;
    }
}

void Spektrometr::startSequence()
{
    if (m_sequenceRunning.load() || (m_sequenceWorkerThread && m_sequenceWorkerThread->isRunning())) {
        return;
    }
    refreshPortLists();

#ifdef Q_OS_WIN
    {
        const QString wantX = ui.comboPortX ? normalizedSerialPortName(ui.comboPortX->currentText()) : normalizedSerialPortName(m_options.port_x);
        const QString wantY = ui.comboPortY ? normalizedSerialPortName(ui.comboPortY->currentText()) : normalizedSerialPortName(m_options.port_y);
        openSerialPort(m_portX, m_openPortX, wantX, "X");
        openSerialPort(m_portY, m_openPortY, wantY, "Y");
    }
#endif

    startPixelink();

    QString reason;
    if (!check_hardware(&reason)) {
        appendLog(reason.isEmpty() ? QStringLiteral("Sequence not started: Hardware not ready")
                                  : QStringLiteral("Sequence not started: %1").arg(reason));
        return;
    }

    QDir root = resolveMeasurementDataDir();
    if (!root.exists()) {
        root.mkpath(QStringLiteral("."));
    }
    const QString folderName = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    root.mkpath(folderName);
    m_sequenceSessionFolder = root.absoluteFilePath(folderName);

    m_sequenceRunning.store(true);
    m_sequencePaused.store(false);
    SequenceRunSnapshot snapshot;
    snapshot.roiMin = int(m_options.spectrum_range_min);
    snapshot.roiMax = int(m_options.spectrum_range_max);
    snapshot.exposures = m_sequenceExposureTimesMs.isEmpty()
        ? QVector<double>{ ui.spinExposureSpectrum ? ui.spinExposureSpectrum->value() : m_options.exposure_time_ms }
        : m_sequenceExposureTimesMs;

    m_sequencePointIndex = 0;
    m_stageHasReference = true;
    m_stageOffsetXUm.store(0);
    m_stageOffsetYUm.store(0);

    appendLog(QStringLiteral("Sequence started"));

    preview_map(snapshot);
}

void Spektrometr::stopSequence()
{
    const bool workerRunning = m_sequenceWorkerThread && m_sequenceWorkerThread->isRunning();
    if (!m_sequenceRunning.load() && !workerRunning) {
        return;
    }
    m_sequenceRunning.store(false);
    m_sequencePaused.store(false);

    if (workerRunning) {
        m_sequenceWorkerThread->requestInterruption();
    } else if (m_stageHasReference) {
        QTimer::singleShot(0, this, [this]() {
            returnStageToSequenceStart();
        });
    }

    setSequenceButtonsEnabled(false);

    appendLog(QStringLiteral("Sequence stopped"));
}

void Spektrometr::launchSequenceWorker(SequenceRunSnapshot snapshot)
{
    if (!m_sequenceRunning.load() || m_sequencePaused.load() || (m_sequenceWorkerThread && m_sequenceWorkerThread->isRunning())) {
        return;
    }

    QThread* workerThread = QThread::create([this, snapshot]() {
        runSequenceLoop(snapshot);
    });
    m_sequenceWorkerThread = workerThread;
    connect(workerThread, &QThread::finished, this, [this, workerThread]() {
        if (m_sequenceWorkerThread == workerThread) {
            m_sequenceWorkerThread = nullptr;
        }
        workerThread->deleteLater();
        if (!m_sequenceRunning.load() && m_stageHasReference) {
            returnStageToSequenceStart();
        }
        setSequenceButtonsEnabled(m_sequenceRunning.load());
    });
    workerThread->start();
}

bool Spektrometr::waitForSequenceRunning()
{
    if (!m_sequenceRunning.load()) {
        return false;
    }

    while (m_sequenceRunning.load() && m_sequencePaused.load() && !QThread::currentThread()->isInterruptionRequested()) {
        QThread::msleep(20);
    }
    return m_sequenceRunning.load() && !QThread::currentThread()->isInterruptionRequested();
}

void Spektrometr::finalizeSequenceSuccess()
{
    m_sequenceRunning.store(false);
    m_sequencePaused.store(false);
    setSequenceButtonsEnabled(false);
    refreshResults();
    returnStageToSequenceStart();
}

void Spektrometr::refreshResults()
{
    if (!ui.listMeasurements) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() {
            refreshResults();
        }, Qt::QueuedConnection);
        return;
    }

    if (m_resultsScanRunning.exchange(true)) {
        m_resultsRefreshPending.store(true);
        return;
    }

    const QString selectedPath = ui.listMeasurements->currentItem()
        ? ui.listMeasurements->currentItem()->data(Qt::UserRole).toString()
        : QString();
    const int selectedRow = ui.listMeasurements->currentRow();
    const QString rootPath = resolveMeasurementDataDir().absolutePath();
    const QPointer<Spektrometr> self(this);

    std::thread([self, rootPath, selectedPath, selectedRow]() mutable {
        const QVector<MeasurementListEntry> entries = collectMeasurementListEntries(rootPath);
        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self.data(), [self, entries = std::move(entries), selectedPath, selectedRow]() mutable {
            if (!self) {
                return;
            }

            auto* list = self->ui.listMeasurements;
            if (!list) {
                self->m_resultsScanRunning = false;
                return;
            }

            QSignalBlocker blocker(list);
            list->setUpdatesEnabled(false);
            list->clear();

            for (const auto& entry : entries) {
                auto* item = new QListWidgetItem(QStringLiteral("%1 (%2 points)").arg(entry.displayName).arg(entry.pointCount), list);
                item->setData(Qt::UserRole, entry.folderPath);
                item->setData(Qt::UserRole + 1, entry.firstCsvPath);
                item->setToolTip(entry.folderPath);
            }

            if (!selectedPath.isEmpty()) {
                for (int row = 0; row < list->count(); ++row) {
                    auto* item = list->item(row);
                    if (item && item->data(Qt::UserRole).toString() == selectedPath) {
                        list->setCurrentRow(row);
                        list->scrollToItem(item, QAbstractItemView::PositionAtCenter);
                        break;
                    }
                }
            } else if (selectedRow >= 0 && selectedRow < list->count()) {
                list->setCurrentRow(selectedRow);
            }

            list->setUpdatesEnabled(true);

            if (self->ui.labelResultsInfo) {
                self->ui.labelResultsInfo->setText(QStringLiteral("Measurements: %1").arg(entries.size()));
            }

            self->m_resultsScanRunning = false;
            if (self->m_resultsRefreshPending.exchange(false)) {
                self->refreshResults();
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void Spektrometr::openSelectedMeasurement()
{
    if (!ui.listMeasurements) {
        return;
    }

    auto* item = ui.listMeasurements->currentItem();
    if (!item) {
        item = ui.listMeasurements->item(0);
    }
    if (!item) {
        return;
    }

    const QString folderPath = item->data(Qt::UserRole).toString();
    const QString firstCsv = item->data(Qt::UserRole + 1).toString();
    if (folderPath.isEmpty()) {
        return;
    }

    QString csvPath = firstCsv;
    if (csvPath.isEmpty()) {
        QDir d(folderPath);
        const auto pointFiles = d.entryInfoList(QStringList() << QStringLiteral("pt_x*_y*.csv"), QDir::Files | QDir::Readable, QDir::Name);
        if (!pointFiles.isEmpty()) {
            csvPath = pointFiles.first().absoluteFilePath();
        }
    }
    if (csvPath.isEmpty()) {
        return;
    }

    auto* win = HeatmapWindow::createLazy(this);
    win->setAttribute(Qt::WA_DeleteOnClose, true);
    win->show();
    QTimer::singleShot(0, win, [win, csvPath]() {
        win->startLoadAsync(csvPath, 0, -1);
    });
}

void Spektrometr::exportAllMeasurements()
{
    const QString targetDir = QFileDialog::getExistingDirectory(this, tr("Export measurements"), QDir::currentPath());
    if (targetDir.isEmpty()) {
        return;
    }

    const QDir root = resolveMeasurementDataDir();
    int copied = 0;
    QDirIterator it(root.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString folderPath = it.next();
        QDir folder(folderPath);
        const auto pointFiles = folder.entryInfoList(QStringList() << QStringLiteral("pt_x*_y*.csv"), QDir::Files | QDir::Readable, QDir::Name);
        if (pointFiles.isEmpty()) {
            continue;
        }

        QString displayName = QFileInfo(folderPath).fileName();
        if (displayName.isEmpty()) {
            displayName = QStringLiteral("measurement_data");
        }

        const QString dstFolder = QDir(targetDir).absoluteFilePath(displayName);
        QDir().mkpath(dstFolder);
        QDir dstDir(dstFolder);

        for (const QFileInfo& srcInfo : pointFiles) {
            const QString dstFile = dstDir.absoluteFilePath(srcInfo.fileName());
            QFile::remove(dstFile);
            if (QFile::copy(srcInfo.absoluteFilePath(), dstFile)) {
                ++copied;
            }
        }
    }

    appendLog(QStringLiteral("Exported %1 measurement folder(s)").arg(copied));
}

void Spektrometr::deleteAllMeasurements()
{
    if (QMessageBox::question(this, tr("Delete all"), tr("Delete all measurement folders?")) != QMessageBox::Yes) {
        return;
    }

    const QDir root = resolveMeasurementDataDir();
    int removed = 0;
    QDirIterator it(root.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString folderPath = it.next();
        QDir folder(folderPath);
        const auto pointFiles = folder.entryInfoList(QStringList() << QStringLiteral("pt_x*_y*.csv"), QDir::Files | QDir::Readable, QDir::Name);
        if (pointFiles.isEmpty()) {
            continue;
        }

        if (folder.removeRecursively()) {
            ++removed;
        }
    }

    appendLog(QStringLiteral("Deleted %1 measurement folder(s)").arg(removed));
    refreshResults();
}

void Spektrometr::deleteSelectedMeasurement()
{
    if (!ui.listMeasurements) {
        return;
    }

    auto* item = ui.listMeasurements->currentItem();
    if (!item) {
        item = ui.listMeasurements->item(0);
    }
    if (!item) {
        return;
    }

    const QString folderPath = item->data(Qt::UserRole).toString();
    if (folderPath.isEmpty()) {
        return;
    }

    if (QMessageBox::question(this, tr("Delete measurement"), tr("Delete selected measurement folder?")) != QMessageBox::Yes) {
        return;
    }

    QDir(folderPath).removeRecursively();
    refreshResults();
}

bool Spektrometr::eventFilter(QObject* watched, QEvent* event)
{
    if (event && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        if (watched == centralWidget() && m_toastHost) {
            m_toastHost->setGeometry(centralWidget()->rect());
        }
        if (watched == ui.labelPixelinkPreview) {
            tickPixelink();
        }
        if (watched == ui.labelSpectrumPlaceholder) {
            tickSpectrum();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void Spektrometr::tryAutoResumeSequence()
{
    if (!m_sequencePaused.load() || !m_sequenceRunning.load()) {
        return;
    }

    QString reason;
    if (check_hardware(&reason)) {
        resumeSequence();
    }
}

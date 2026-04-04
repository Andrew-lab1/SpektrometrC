#include "HeatmapWindow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QTextStream>
#include <QVBoxLayout>
#include <QPointer>
#include <QApplication>

#include <thread>
#include <vector>
#include <cmath>

#if __has_include(<opencv2/opencv.hpp>)
#include <opencv2/opencv.hpp>
#define SPEKTROMETR_HAS_OPENCV 1
#else
#define SPEKTROMETR_HAS_OPENCV 0
#endif

static bool readPointSpectrumColumn(const QString& path, int exposureCol1Based, QVector<double>* outSpectrum)
{
    if (!outSpectrum) return false;
    outSpectrum->clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&f);
    bool firstLine = true;
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (firstLine) {
            firstLine = false;
            continue; // header
        }

        const QStringList parts = line.split(',', Qt::KeepEmptyParts);
        // parts[0]=lambda, parts[1..]=exposures
        const int idx = qBound(1, exposureCol1Based, parts.size() - 1);
        if (parts.size() <= idx) {
            outSpectrum->push_back(0.0);
            continue;
        }
        bool okV = false;
        const double v = parts[idx].toDouble(&okV);
        outSpectrum->push_back(okV ? v : 0.0);
    }
    return !outSpectrum->isEmpty();
}

static bool parsePointFileNameLocal(const QString& fileName, int* xOut, int* yOut)
{
    if (!fileName.startsWith(QStringLiteral("pt_x")) || !fileName.endsWith(QStringLiteral(".csv"))) {
        return false;
    }
    const int ix = fileName.indexOf(QStringLiteral("_y"));
    if (ix < 0) return false;
    const QString xStr = fileName.mid(4, ix - 4);
    const QString yStr = fileName.mid(ix + 2, fileName.size() - (ix + 2) - 4);
    bool okX = false;
    bool okY = false;
    const int x = xStr.toInt(&okX);
    const int y = yStr.toInt(&okY);
    if (!okX || !okY) return false;
    if (xOut) *xOut = x;
    if (yOut) *yOut = y;
    return true;
}

static QStringList parsePointFileHeaderExposuresLocal(const QString& headerLine)
{
    QStringList parts = headerLine.trimmed().split(',', Qt::KeepEmptyParts);
    if (parts.size() < 2) return {};
    parts.removeFirst();
    for (QString& p : parts) p = p.trimmed();
    return parts;
}

static bool loadPointFolderData(const QString& anyPointCsvPath,
    QVector<HeatmapWindow::MeasurementPoint>* outPoints,
    QStringList* outExposureLabels,
    QString* outFolderPath,
    QString* errOut)
{
    if (errOut) errOut->clear();
    if (outPoints) outPoints->clear();
    if (outExposureLabels) outExposureLabels->clear();
    if (outFolderPath) outFolderPath->clear();

    const QFileInfo fi(anyPointCsvPath);
    const QDir folder = fi.absoluteDir();
    if (outFolderPath) *outFolderPath = folder.absolutePath();

    QFile f(anyPointCsvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errOut) *errOut = QStringLiteral("Cannot open point CSV: %1").arg(anyPointCsvPath);
        return false;
    }
    QTextStream in(&f);
    if (in.atEnd()) {
        if (errOut) *errOut = QStringLiteral("Empty point CSV: %1").arg(anyPointCsvPath);
        return false;
    }
    const QString header = in.readLine();
    const QStringList exposureLabels = parsePointFileHeaderExposuresLocal(header);
    if (exposureLabels.isEmpty()) {
        if (errOut) *errOut = QStringLiteral("No exposure columns in point CSV header");
        return false;
    }
    if (outExposureLabels) *outExposureLabels = exposureLabels;

    QDir d(folder);
    const auto files = d.entryInfoList(QStringList() << QStringLiteral("pt_x*_y*.csv"), QDir::Files | QDir::Readable, QDir::Name);
    if (files.isEmpty()) {
        if (errOut) *errOut = QStringLiteral("No pt_x*_y*.csv files found in folder");
        return false;
    }

    QVector<HeatmapWindow::MeasurementPoint> points;
    points.reserve(files.size());

    const int exposureCol1Based = 1;
    int spectrumLen = -1;
    for (const QFileInfo& pfi : files) {
        int x = 0;
        int y = 0;
        if (!parsePointFileNameLocal(pfi.fileName(), &x, &y)) {
            continue;
        }
        HeatmapWindow::MeasurementPoint p;
        p.x = x;
        p.y = y;
        if (!readPointSpectrumColumn(pfi.absoluteFilePath(), exposureCol1Based, &p.spectrum)) {
            continue;
        }
        if (spectrumLen < 0) spectrumLen = p.spectrum.size();
        else if (p.spectrum.size() != spectrumLen) p.spectrum.resize(spectrumLen);
        points.push_back(std::move(p));
    }

    if (points.isEmpty()) {
        if (errOut) *errOut = QStringLiteral("No valid point spectra loaded");
        return false;
    }

    if (outPoints) *outPoints = std::move(points);
    return true;
}

static bool loadLegacyCsvPointsData(const QString& csvPath,
    QVector<HeatmapWindow::MeasurementPoint>* outPoints,
    QString* errOut)
{
    if (errOut) errOut->clear();
    if (outPoints) outPoints->clear();

    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errOut) *errOut = QStringLiteral("Cannot open CSV: %1").arg(csvPath);
        return false;
    }

    QTextStream in(&f);
    QVector<HeatmapWindow::MeasurementPoint> points;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QStringList parts = line.split(',', Qt::KeepEmptyParts);
        if (parts.size() < 3) continue;

        bool okX = false;
        bool okY = false;
        const int x = int(parts[0].toDouble(&okX));
        const int y = int(parts[1].toDouble(&okY));
        if (!okX || !okY) continue;

        HeatmapWindow::MeasurementPoint p;
        p.x = x;
        p.y = y;
        p.spectrum.reserve(parts.size() - 2);
        for (int i = 2; i < parts.size(); ++i) {
            bool okV = false;
            const double v = parts[i].toDouble(&okV);
            p.spectrum.push_back(okV ? v : 0.0);
        }
        points.push_back(std::move(p));
    }

    if (points.isEmpty()) {
        if (errOut) *errOut = QStringLiteral("CSV contains no points");
        return false;
    }
    if (outPoints) *outPoints = std::move(points);
    return true;
}

static QColor mapColorForPalette(const QString& cmap, int c)
{
    const int value = qBound(0, c, 255);
    if (cmap == QStringLiteral("gray")) {
        return QColor(value, value, value);
    }
    if (cmap == QStringLiteral("hot")) {
        const int r = qMin(255, value * 2);
        const int g = (value > 128) ? qMin(255, (value - 128) * 2) : 0;
        return QColor(r, g, 0);
    }
    if (cmap == QStringLiteral("jet")) {
        const double tt = double(value) / 255.0;
        const int r = qBound(0, int(255.0 * qMin(1.0, qMax(0.0, 1.5 - qAbs(4.0 * tt - 3.0)))), 255);
        const int g = qBound(0, int(255.0 * qMin(1.0, qMax(0.0, 1.5 - qAbs(4.0 * tt - 2.0)))), 255);
        const int b = qBound(0, int(255.0 * qMin(1.0, qMax(0.0, 1.5 - qAbs(4.0 * tt - 1.0)))), 255);
        return QColor(r, g, b);
    }

    // viridis (simple 5-point LUT)
    static const QColor lut[] = {
        QColor(68, 1, 84), QColor(59, 82, 139), QColor(33, 145, 140), QColor(94, 201, 98), QColor(253, 231, 37)
    };
    const double tt = double(value) / 255.0;
    const int segs = 4;
    const double pos = tt * segs;
    const int i0 = qBound(0, int(pos), segs - 1);
    const int i1 = i0 + 1;
    const double a = pos - double(i0);
    const QColor c0 = lut[i0];
    const QColor c1 = lut[i1];
    const int r = int(c0.red() + a * (c1.red() - c0.red()));
    const int g = int(c0.green() + a * (c1.green() - c0.green()));
    const int b = int(c0.blue() + a * (c1.blue() - c0.blue()));
    return QColor(r, g, b);
}

static QVector<double> computeMeanSpectrum(const QVector<QVector<double>>& cube, int mn, int mx)
{
    QVector<double> mean;
    if (cube.isEmpty() || mx < mn) {
        return mean;
    }

    const int len = qMin(cube.size(), mx + 1) - qMax(0, mn);
    if (len <= 0) {
        return mean;
    }

    mean.fill(0.0, mx - mn + 1);
    for (int gk = mn; gk <= mx && gk < cube.size(); ++gk) {
        const auto& slice = cube[gk];
        if (slice.isEmpty()) {
            mean[gk - mn] = 0.0;
            continue;
        }

        double sum = 0.0;
        for (double v : slice) {
            sum += v;
        }
        mean[gk - mn] = sum / double(slice.size());
    }

    return mean;
}

static QImage buildHeatmapImage(const QVector<double>& slice, int nx, int ny, const QString& cmap, double* minOut = nullptr, double* maxOut = nullptr)
{
    if (minOut) *minOut = 0.0;
    if (maxOut) *maxOut = 1.0;

    if (nx <= 0 || ny <= 0) {
        return {};
    }

    double minV = slice.isEmpty() ? 0.0 : slice[0];
    double maxV = slice.isEmpty() ? 1.0 : slice[0];
    for (double v : slice) {
        minV = qMin(minV, v);
        maxV = qMax(maxV, v);
    }
    if (qFuzzyCompare(minV, maxV)) {
        maxV = minV + 1.0;
    }

    QImage img(nx, ny, QImage::Format_ARGB32_Premultiplied);
    if (slice.isEmpty()) {
        img.fill(Qt::black);
    }

    for (int y = 0; y < ny; ++y) {
        const int yy = (ny - 1) - y;
        for (int x = 0; x < nx; ++x) {
            const int idx = y * nx + x;
            const double v = (idx < slice.size()) ? slice[idx] : 0.0;
            const double t = (v - minV) / (maxV - minV);
            const int c = qBound(0, int(t * 255.0), 255);
            img.setPixelColor(x, yy, mapColorForPalette(cmap, c));
        }
    }

    if (minOut) *minOut = minV;
    if (maxOut) *maxOut = maxV;
    return img;
}

static QImage buildColorbarImage(double minV, double maxV, const QString& cmap, const QSize& size)
{
    const int w = qMax(28, size.width());
    const int h = qMax(160, size.height());

    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(10, 10, 12));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int barW = qMax(14, w / 3);
    const int barX0 = 6;
    const int barX1 = barX0 + barW;
    const int top = 8;
    const int bottom = h - 8;

    for (int y = top; y <= bottom; ++y) {
        const double tt = (bottom == top) ? 0.0 : double(y - top) / double(bottom - top);
        const int c = qBound(0, int((1.0 - tt) * 255.0), 255);
        p.setPen(mapColorForPalette(cmap, c));
        p.drawLine(barX0, y, barX1, y);
    }

    p.setPen(QPen(QColor(200, 200, 210), 1));
    p.drawRect(QRect(barX0, top, barW, bottom - top));

    p.setPen(QColor(220, 220, 230));
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);

    const int textX = barX1 + 6;
    p.drawText(QRect(textX, top - 2, w - textX - 2, 16), Qt::AlignLeft | Qt::AlignTop, QString::number(maxV, 'g', 4));
    p.drawText(QRect(textX, bottom - 14, w - textX - 2, 16), Qt::AlignLeft | Qt::AlignBottom, QString::number(minV, 'g', 4));

    return img;
}

static QImage buildSpectrumImage(const QVector<double>& mean, int currentLambda, int mn, int mx, const QSize& size)
{
    const int w = qMax(600, size.width());
    const int h = qMax(220, size.height());
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    {
        QPainter bgp(&img);
        QLinearGradient bg(0, 0, 0, h);
        bg.setColorAt(0.0, QColor(14, 14, 16));
        bg.setColorAt(1.0, QColor(8, 8, 10));
        bgp.fillRect(QRect(0, 0, w, h), bg);
    }

    if (mean.isEmpty()) {
        QPainter p(&img);
        p.setPen(QColor(220, 220, 220));
        p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, QObject::tr("Brak danych"));
        return img;
    }

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QFont tickFont(QFont(p.font().family(), 8));
    const QFont titleFont(QFont(p.font().family(), 9, QFont::DemiBold));
    const QFontMetrics fmTicks(tickFont);
    const QFontMetrics fmTitle(titleFont);

    const int left = qMax(64, fmTicks.horizontalAdvance(QStringLiteral("0000.0")) + 18);
    const int right = qMax(18, fmTicks.horizontalAdvance(QStringLiteral("000")) / 2);
    const int top = qMax(12, fmTitle.height() + 8);
    const int bottom = qMax(54, fmTicks.height() * 2 + 18);
    const QRect plot(left, top, qMax(10, w - left - right), qMax(10, h - top - bottom));

    double minV = mean[0];
    double maxV = mean[0];
    for (double v : mean) {
        minV = qMin(minV, v);
        maxV = qMax(maxV, v);
    }
    if (qFuzzyCompare(minV, maxV)) {
        maxV = minV + 1.0;
    }
    const double yPad = (maxV - minV) * 0.08;
    minV -= yPad;
    maxV += yPad;

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

    QPolygonF curve;
    curve.reserve(mean.size());
    for (int k = 0; k < mean.size(); ++k) {
        const double t = (mean.size() == 1) ? 0.0 : double(k) / double(mean.size() - 1);
        const qreal x = plot.left() + qreal(t) * qreal(plot.width());
        const double vn = (mean[k] - minV) / (maxV - minV);
        const qreal y = plot.bottom() - qreal(vn) * qreal(plot.height());
        curve.push_back(QPointF(x, y));
    }

    {
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
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 255, 180, 60), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(curve);
    p.setPen(QPen(QColor(0, 230, 150), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(curve);

    if (mean.size() > 1) {
        const int roiIx = qBound(0, currentLambda - mn, qMax(0, mean.size() - 1));
        const double t = double(roiIx) / double(mean.size() - 1);
        const int x = plot.left() + int(std::round(t * plot.width()));
        p.setPen(QPen(QColor(255, 90, 90), 2, Qt::DashLine));
        p.drawLine(QPoint(x, plot.top()), QPoint(x, plot.bottom()));
    }

    p.setPen(QColor(230, 230, 235));
    p.setFont(titleFont);
    p.drawText(QRect(8, 6, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Spectrum"));

    p.setFont(QFont(p.font().family(), 9));
    p.setPen(QColor(165, 165, 175));
    p.drawText(QRect(8, h - 22, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Wavelength index: %1 (ROI %2..%3)")
            .arg(currentLambda)
            .arg(mn)
            .arg(mx));

    p.setFont(tickFont);
    p.setPen(QColor(130, 130, 140));
    p.drawText(QRect(0, plot.top() - 6, left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxV, 'g', 4));
    p.drawText(QRect(0, plot.bottom() - (fmTicks.height() / 2), left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(minV, 'g', 4));

    {
        p.setPen(QColor(130, 130, 140));
        const int ticks = 5;
        const int span = qMax(1, mx - mn);
        for (int i = 0; i <= ticks; ++i) {
            const double t = double(i) / double(ticks);
            const int globalIdx = mn + int(std::round(t * span));
            const int x = plot.left() + int(std::round(t * plot.width()));
            p.drawLine(QPoint(x, plot.bottom()), QPoint(x, plot.bottom() + 4));

            const QString s = QString::number(globalIdx);
            const int tw = fmTicks.horizontalAdvance(s);
            p.drawText(QRect(x - tw / 2, plot.bottom() + 6, tw + 2, fmTicks.height()), Qt::AlignHCenter | Qt::AlignTop, s);
        }
    }

    p.setPen(QColor(190, 190, 200));
    p.drawText(QRect(plot.left(), plot.bottom() + (fmTicks.height() * 2 + 12), plot.width(), fmTicks.height() + 2), Qt::AlignCenter, QStringLiteral("Wavelength"));
    p.save();
    p.translate(18, plot.top() + plot.height() / 2);
    p.rotate(-90);
    p.drawText(QRect(-plot.height() / 2, -12, plot.height(), 18), Qt::AlignCenter, QStringLiteral("Intensity"));
    p.restore();

    return img;
}

HeatmapWindow* HeatmapWindow::createLazy(QWidget* parent)
{
    auto* w = new HeatmapWindow(QString(), 0, -1, parent);
    w->m_lazyMode = true;
    w->initUiForLazy();
    return w;
}

void HeatmapWindow::initUiForLazy()
{
    setWindowTitle(QStringLiteral("Heatmap"));
    if (m_heatmapLabel) m_heatmapLabel->clear();
    if (m_colorbarLabel) m_colorbarLabel->clear();
    if (m_spectrumLabel) m_spectrumLabel->clear();
    if (m_slider) {
        const QSignalBlocker b(m_slider);
        m_slider->setMinimum(0);
        m_slider->setMaximum(0);
        m_slider->setValue(0);
        m_slider->setEnabled(false);
    }
}

void HeatmapWindow::startLoadAsync(const QString& csvPath, int roiMinIdx, int roiMaxIdx)
{
    m_pendingCsv = csvPath;
    m_pendingRoiMin = roiMinIdx;
    m_pendingRoiMax = roiMaxIdx;

    // Disable interactive widgets until loaded.
    if (m_roiMinSpin) m_roiMinSpin->setEnabled(false);
    if (m_roiMaxSpin) m_roiMaxSpin->setEnabled(false);
    if (m_roiResetBtn) m_roiResetBtn->setEnabled(false);
    if (m_exposureCombo) m_exposureCombo->setEnabled(false);
    if (m_colormapCombo) m_colormapCombo->setEnabled(false);

    if (m_heatmapLabel) m_heatmapLabel->setText(tr("Loading..."));
    if (m_spectrumLabel) m_spectrumLabel->setText(tr("Loading..."));

    const QPointer<HeatmapWindow> self(this);
    ::std::thread([self, csvPath]() {
        QVector<MeasurementPoint> points;
        QStringList exposureLabels;
        QString pointFolderPath;
        QString err;

        if (self.isNull()) {
            return;
        }

        const QFileInfo fi(csvPath);
        const bool isPointFolder = parsePointFileNameLocal(fi.fileName(), nullptr, nullptr);

        bool ok = false;
        if (isPointFolder) {
            ok = loadPointFolderData(csvPath, &points, &exposureLabels, &pointFolderPath, &err);
        } else {
            ok = loadLegacyCsvPointsData(csvPath, &points, &err);
        }

        QMetaObject::invokeMethod(qApp, [self, ok, points, exposureLabels, pointFolderPath, isPointFolder, err]() mutable {
            if (self.isNull()) {
                return;
            }
            auto* w = self.data();
            if (!ok) {
                QMessageBox::warning(w, QObject::tr("Heatmap"), QObject::tr("Failed to load: %1").arg(err));
                w->close();
                return;
            }

            w->m_isPointFolder = isPointFolder;
            w->m_pointFolderPath = pointFolderPath;
            w->m_pointExposureLabels = exposureLabels;
            w->m_pointExposureColumn = 1;

            if (w->m_exposureCombo) {
                const QSignalBlocker b(w->m_exposureCombo);
                w->m_exposureCombo->clear();
                if (w->m_isPointFolder && !w->m_pointExposureLabels.isEmpty()) {
                    for (const QString& lbl : w->m_pointExposureLabels) {
                        w->m_exposureCombo->addItem(lbl);
                    }
                } else {
                    w->m_exposureCombo->addItem(QStringLiteral("Primary"));
                }
                w->m_exposureCombo->setCurrentIndex(0);
            }

            if (!w->loadCsvFromPoints(points)) {
                QMessageBox::warning(w, QObject::tr("Heatmap"), QObject::tr("Failed to load points"));
                w->close();
                return;
            }

            w->buildCube();
            w->finishLoadOnUiThread(w->m_pendingRoiMin, w->m_pendingRoiMax);
        }, Qt::QueuedConnection);
    }).detach();
}

void HeatmapWindow::finishLoadOnUiThread(int roiMinIdx, int roiMaxIdx)
{
    const int fullMin = 0;
    const int fullMax = qMax(0, m_spectrumLen - 1);
    const int wantMin = qBound(fullMin, roiMinIdx, fullMax);
    const int wantMax = (roiMaxIdx < 0) ? fullMax : qBound(fullMin, roiMaxIdx, fullMax);

    m_currentRoiMin = qMin(wantMin, wantMax);
    m_currentRoiMax = qMax(wantMin, wantMax);
    m_currentLambda = qBound(m_currentRoiMin, m_currentLambda, m_currentRoiMax);

    if (m_roiMinSpin) {
        const QSignalBlocker b(m_roiMinSpin);
        m_roiMinSpin->setValue(m_currentRoiMin);
    }
    if (m_roiMaxSpin) {
        const QSignalBlocker b(m_roiMaxSpin);
        m_roiMaxSpin->setValue(m_currentRoiMax);
    }

    if (m_slider) {
        const QSignalBlocker b(m_slider);
        m_slider->setMaximum(qMax(0, roiLen() - 1));
        m_slider->setValue(qBound(0, roiIdxFromGlobalIdx(m_currentLambda), qMax(0, roiLen() - 1)));
        m_slider->setEnabled(true);
    }

    if (m_roiMinSpin) m_roiMinSpin->setEnabled(true);
    if (m_roiMaxSpin) m_roiMaxSpin->setEnabled(true);
    if (m_roiResetBtn) m_roiResetBtn->setEnabled(true);
    if (m_exposureCombo) m_exposureCombo->setEnabled(true);
    if (m_colormapCombo) m_colormapCombo->setEnabled(true);

    scheduleRender();

    if (!isVisible()) {
        show();
    }

    // Notify listeners that the window is ready (used to hide loading overlay).
    setWindowTitle(windowTitle());
}

HeatmapWindow::HeatmapWindow(const QString& csvPath, QWidget* parent)
    : HeatmapWindow(csvPath, 0, -1, parent)
{
}

HeatmapWindow::HeatmapWindow(const QString& csvPath, int roiMinIdx, int roiMaxIdx, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Heatmap"));
    setMinimumSize(900, 620);

    setStyleSheet(QStringLiteral(
        "QDialog{background:#0b0c0f;color:#e6e6e6;}"
        "QLabel{color:#e6e6e6;}"
        "QComboBox{background:#14161c;color:#e6e6e6;border:1px solid #2a2d36;padding:3px;}"
        "QSlider::groove:horizontal{height:6px;background:#2a2d36;border-radius:3px;}"
        "QSlider::handle:horizontal{background:#6ee7b7;width:14px;margin:-5px 0;border-radius:7px;}"
    ));

    // Build UI in code (no .ui file)
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);

    auto* header = new QLabel(this);
    header->setText(QStringLiteral("File: %1").arg(QFileInfo(csvPath).fileName()));
    root->addWidget(header);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(tr("Exposure:"), this));
    m_exposureCombo = new QComboBox(this);
    m_exposureCombo->setMinimumWidth(140);
    controls->addWidget(m_exposureCombo);

    controls->addSpacing(10);

    controls->addWidget(new QLabel(tr("ROI:"), this));
    m_roiMinSpin = new QDoubleSpinBox(this);
    m_roiMinSpin->setDecimals(0);
    m_roiMinSpin->setMinimum(0);
    m_roiMinSpin->setMaximum(999999);
    m_roiMinSpin->setKeyboardTracking(false);
    m_roiMinSpin->setFixedWidth(80);
    controls->addWidget(m_roiMinSpin);

    controls->addWidget(new QLabel(tr("to"), this));
    m_roiMaxSpin = new QDoubleSpinBox(this);
    m_roiMaxSpin->setDecimals(0);
    m_roiMaxSpin->setMinimum(0);
    m_roiMaxSpin->setMaximum(999999);
    m_roiMaxSpin->setKeyboardTracking(false);
    m_roiMaxSpin->setFixedWidth(80);
    controls->addWidget(m_roiMaxSpin);

    m_roiResetBtn = new QPushButton(tr("Reset"), this);
    m_roiResetBtn->setFixedWidth(70);
    controls->addWidget(m_roiResetBtn);

    controls->addSpacing(10);

    controls->addWidget(new QLabel(tr("Palette:"), this));
    m_colormapCombo = new QComboBox(this);
    m_colormapCombo->addItems({QStringLiteral("gray"), QStringLiteral("hot"), QStringLiteral("viridis"), QStringLiteral("jet")});
    m_colormapCombo->setCurrentText(QStringLiteral("viridis"));
    controls->addWidget(m_colormapCombo);

    controls->addStretch(1);
    root->addLayout(controls);

    // Heatmap + colorbar row (top)
    auto* heatRow = new QHBoxLayout();

    m_heatmapLabel = new QLabel(this);
    m_heatmapLabel->setObjectName("heatmapLabel");
    m_heatmapLabel->setMinimumSize(600, 280);
    m_heatmapLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_heatmapLabel->setFrameShape(QFrame::StyledPanel);
    m_heatmapLabel->setAlignment(Qt::AlignCenter);
    heatRow->addWidget(m_heatmapLabel, 1);

    m_colorbarLabel = new QLabel(this);
    m_colorbarLabel->setObjectName("colorbarLabel");
    m_colorbarLabel->setMinimumWidth(56);
    m_colorbarLabel->setMaximumWidth(80);
    m_colorbarLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_colorbarLabel->setFrameShape(QFrame::StyledPanel);
    m_colorbarLabel->setAlignment(Qt::AlignCenter);
    heatRow->addWidget(m_colorbarLabel, 0);

    root->addLayout(heatRow, 2);

    // Wavelength slider (middle)
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName("lambdaSlider");
    m_slider->setMinimum(0);
    m_slider->setMaximum(0);
    m_slider->setFixedHeight(26);
    root->addWidget(m_slider);

    // Spectrum (bottom)
    m_spectrumLabel = new QLabel(this);
    m_spectrumLabel->setObjectName("spectrumLabel");
    m_spectrumLabel->setMinimumHeight(220);
    m_spectrumLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_spectrumLabel->setFrameShape(QFrame::StyledPanel);
    m_spectrumLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_spectrumLabel, 1);

    // Resize -> rescale cached images without recomputing data.
    installEventFilter(this);
    if (centralWidget()) {
        centralWidget()->installEventFilter(this);
    }
    if (m_heatmapLabel) m_heatmapLabel->installEventFilter(this);
    if (m_colorbarLabel) m_colorbarLabel->installEventFilter(this);
    if (m_spectrumLabel) m_spectrumLabel->installEventFilter(this);
    
    m_resizeRenderTimer = new QTimer(this);
    m_resizeRenderTimer->setSingleShot(true);
    m_resizeRenderTimer->setInterval(75);
    connect(m_resizeRenderTimer, &QTimer::timeout, this, [this]() {
        scheduleRender();
    });

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) { onSliderChanged(v); });
    connect(m_exposureCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        rebuildFromCurrentPoints();
    });

    connect(m_colormapCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        scheduleRender();
    });

    if (m_roiMinSpin) {
        connect(m_roiMinSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { onRoiChanged(); });
    }
    if (m_roiMaxSpin) {
        connect(m_roiMaxSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { onRoiChanged(); });
    }
    if (m_roiResetBtn) {
        connect(m_roiResetBtn, &QPushButton::clicked, this, [this]() { resetRoiToFull(); });
    }

    // Lazy mode (async loading will be triggered by startLoadAsync).
    // When created with empty path, skip any immediate loading/processing.
    const bool lazy = csvPath.trimmed().isEmpty();
    if (lazy) {
        m_lazyMode = true;
        initUiForLazy();
        return;
    }

    // Detect new folder-based format (pt_x*_y*.csv)
    const QFileInfo fi(csvPath);
    if (parsePointFileName(fi.fileName(), nullptr, nullptr)) {
        m_isPointFolder = true;
        if (!loadPointFolderFromAnyFile(csvPath)) {
            QMessageBox::warning(this, tr("Heatmap"), tr("Failed to load point-folder measurement."));
            return;
        }
    } else {
        discoverExposureFiles(csvPath);

        if (!loadCsv(csvPath)) {
            QMessageBox::warning(this, tr("Heatmap"), tr("Failed to load CSV."));
            return;
        }
    }

    buildCube();

    // Apply ROI from caller (fallback to full range).
    const int fullMin = 0;
    const int fullMax = qMax(0, m_spectrumLen - 1);
    const int wantMin = qBound(fullMin, roiMinIdx, fullMax);
    const int wantMax = (roiMaxIdx < 0) ? fullMax : qBound(fullMin, roiMaxIdx, fullMax);

    m_currentRoiMin = qMin(wantMin, wantMax);
    m_currentRoiMax = qMax(wantMin, wantMax);
    m_currentLambda = qBound(m_currentRoiMin, m_currentLambda, m_currentRoiMax);

    if (m_roiMinSpin) {
        const QSignalBlocker b(m_roiMinSpin);
        m_roiMinSpin->setValue(m_currentRoiMin);
    }
    if (m_roiMaxSpin) {
        const QSignalBlocker b(m_roiMaxSpin);
        m_roiMaxSpin->setValue(m_currentRoiMax);
    }

    if (m_slider) {
        const QSignalBlocker b(m_slider);
        m_slider->setMaximum(qMax(0, roiLen() - 1));
        m_slider->setValue(qBound(0, roiIdxFromGlobalIdx(m_currentLambda), qMax(0, roiLen() - 1)));
    }

    // Ensure initial fit to labels so controls don't overlap on first show.
    scheduleRender();
}

HeatmapWindow::~HeatmapWindow() = default;

bool HeatmapWindow::loadCsv(const QString& csvPath)
{
    if (m_isPointFolder) {
        // Folder-based format is handled elsewhere.
        return false;
    }
    bool ok = false;
    const auto pts = loadCsvPoints(csvPath, &ok);
    if (!ok) {
        return false;
    }
    return loadCsvFromPoints(pts);
}

bool HeatmapWindow::parsePointFileName(const QString& fileName, int* xOut, int* yOut)
{
    // Expect pt_x{X}_y{Y}.csv
    if (!fileName.startsWith(QStringLiteral("pt_x")) || !fileName.endsWith(QStringLiteral(".csv"))) {
        return false;
    }
    const int ix = fileName.indexOf(QStringLiteral("_y"));
    if (ix < 0) {
        return false;
    }
    const QString xStr = fileName.mid(4, ix - 4);
    const QString yStr = fileName.mid(ix + 2, fileName.size() - (ix + 2) - 4);
    bool okX = false;
    bool okY = false;
    const int x = xStr.toInt(&okX);
    const int y = yStr.toInt(&okY);
    if (!okX || !okY) {
        return false;
    }
    if (xOut) *xOut = x;
    if (yOut) *yOut = y;
    return true;
}

QStringList HeatmapWindow::parsePointFileHeaderExposures(const QString& headerLine)
{
    // header: lambda_nm,t_1_ms,t_2_ms,...
    QStringList parts = headerLine.trimmed().split(',', Qt::KeepEmptyParts);
    if (parts.size() < 2) {
        return {};
    }
    parts.removeFirst();
    for (QString& p : parts) {
        p = p.trimmed();
    }
    return parts;
}

bool HeatmapWindow::loadPointFolderFromAnyFile(const QString& anyPointCsvPath)
{
    const QFileInfo fi(anyPointCsvPath);
    const QDir folder = fi.absoluteDir();
    m_pointFolderPath = folder.absolutePath();

    // Read exposure columns from header of provided file
    QFile f(anyPointCsvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&f);
    if (in.atEnd()) {
        return false;
    }
    const QString header = in.readLine();
    m_pointExposureLabels = parsePointFileHeaderExposures(header);
    if (m_pointExposureLabels.isEmpty()) {
        return false;
    }

    // Setup exposure combo for folder-based format
    if (m_exposureCombo) {
        const QSignalBlocker b(m_exposureCombo);
        m_exposureCombo->clear();
        for (const QString& lbl : m_pointExposureLabels) {
            m_exposureCombo->addItem(lbl);
        }
        m_exposureCombo->setCurrentIndex(qBound(0, 0, m_pointExposureLabels.size() - 1));
    }
    m_pointExposureColumn = 1; // first exposure column

    // Load all points for selected exposure column
    QDir d(folder);
    const auto files = d.entryInfoList(QStringList() << QStringLiteral("pt_x*_y*.csv"), QDir::Files | QDir::Readable, QDir::Name);
    if (files.isEmpty()) {
        return false;
    }

    QVector<MeasurementPoint> points;
    points.reserve(files.size());

    int spectrumLen = -1;
    for (const QFileInfo& pfi : files) {
        int x = 0;
        int y = 0;
        if (!parsePointFileName(pfi.fileName(), &x, &y)) {
            continue;
        }
        MeasurementPoint p;
        p.x = x;
        p.y = y;
        if (!readPointSpectrumColumn(pfi.absoluteFilePath(), m_pointExposureColumn, &p.spectrum)) {
            continue;
        }
        if (spectrumLen < 0) {
            spectrumLen = p.spectrum.size();
        } else {
            // normalize length
            if (p.spectrum.size() != spectrumLen) {
                p.spectrum.resize(spectrumLen);
            }
        }
        points.push_back(std::move(p));
    }

    if (!loadCsvFromPoints(points)) {
        return false;
    }

    // refresh title to show folder
    setWindowTitle(QStringLiteral("Heatmap (%1)").arg(QFileInfo(m_pointFolderPath).fileName()));
    return true;
}

QVector<HeatmapWindow::MeasurementPoint> HeatmapWindow::loadCsvPoints(const QString& csvPath, bool* ok) const
{
    if (ok) {
        *ok = false;
    }

    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QTextStream in(&f);
    QVector<MeasurementPoint> points;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(',', Qt::KeepEmptyParts);
        if (parts.size() < 3) {
            continue;
        }

        bool okX = false;
        bool okY = false;
        const int x = int(parts[0].toDouble(&okX));
        const int y = int(parts[1].toDouble(&okY));
        if (!okX || !okY) {
            continue;
        }

        MeasurementPoint p;
        p.x = x;
        p.y = y;
        p.spectrum.reserve(parts.size() - 2);
        for (int i = 2; i < parts.size(); ++i) {
            bool okV = false;
            const double v = parts[i].toDouble(&okV);
            p.spectrum.push_back(okV ? v : 0.0);
        }
        points.push_back(std::move(p));
    }

    if (points.isEmpty()) {
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return points;
}

bool HeatmapWindow::loadCsvFromPoints(const QVector<MeasurementPoint>& points)
{
    if (points.isEmpty()) {
        return false;
    }

    QSet<int> xs;
    QSet<int> ys;
    for (const auto& p : points) {
        xs.insert(p.x);
        ys.insert(p.y);
    }

    m_points = points;

    m_xValues = xs.values().toVector();
    m_yValues = ys.values().toVector();
    std::sort(m_xValues.begin(), m_xValues.end());
    std::sort(m_yValues.begin(), m_yValues.end());

    m_nx = m_xValues.size();
    m_ny = m_yValues.size();
    m_spectrumLen = m_points[0].spectrum.size();

    return m_nx > 0 && m_ny > 0 && m_spectrumLen > 0;
}

void HeatmapWindow::rebuildFromCurrentPoints()
{
    if (m_isPointFolder) {
        if (!m_exposureCombo) {
            return;
        }
        const int idx = m_exposureCombo->currentIndex();
        if (idx < 0 || idx >= m_pointExposureLabels.size()) {
            return;
        }
        m_pointExposureColumn = 1 + idx;

        const QDir folder(m_pointFolderPath);
        const auto files = folder.entryInfoList(QStringList() << QStringLiteral("pt_x*_y*.csv"), QDir::Files | QDir::Readable, QDir::Name);
        if (files.isEmpty()) {
            return;
        }

        QVector<MeasurementPoint> points;
        points.reserve(files.size());
        int spectrumLen = -1;
        for (const QFileInfo& pfi : files) {
            int x = 0;
            int y = 0;
            if (!parsePointFileName(pfi.fileName(), &x, &y)) {
                continue;
            }
            MeasurementPoint p;
            p.x = x;
            p.y = y;
            if (!readPointSpectrumColumn(pfi.absoluteFilePath(), m_pointExposureColumn, &p.spectrum)) {
                continue;
            }
            if (spectrumLen < 0) {
                spectrumLen = p.spectrum.size();
            } else if (p.spectrum.size() != spectrumLen) {
                p.spectrum.resize(spectrumLen);
            }
            points.push_back(std::move(p));
        }

        if (!loadCsvFromPoints(points)) {
            return;
        }
        buildCube();

        // keep slider valid
        const int prev = m_currentLambda;
        if (m_slider) {
            m_slider->setMaximum(qMax(0, m_spectrumLen - 1));
            m_slider->setValue(qBound(0, prev, qMax(0, m_spectrumLen - 1)));
        }
        onSliderChanged(m_slider ? m_slider->value() : 0);
        return;
    }

    if (!m_exposureCombo || m_exposureCombo->currentIndex() < 0 || m_exposureCombo->currentIndex() >= m_sources.size()) {
        return;
    }
    const QString csv = m_sources[m_exposureCombo->currentIndex()].csvPath;
    bool ok = false;
    auto pts = loadCsvPoints(csv, &ok);
    if (!ok) {
        return;
    }

    if (!loadCsvFromPoints(pts)) {
        return;
    }
    buildCube();

    // keep slider valid
    const int prev = m_currentLambda;
    if (m_slider) {
        m_slider->setMaximum(qMax(0, m_spectrumLen - 1));
        m_slider->setValue(qBound(0, prev, qMax(0, m_spectrumLen - 1)));
    }
    onSliderChanged(m_slider ? m_slider->value() : 0);
}

void HeatmapWindow::scheduleRender()
{
    if (!m_heatmapLabel || !m_spectrumLabel || m_nx <= 0 || m_ny <= 0 || m_spectrumLen <= 0 || m_cube.isEmpty()) {
        return;
    }

    if (m_renderWorkerRunning.exchange(true)) {
        m_renderPending = true;
        return;
    }

    const int currentLambda = qBound(0, m_currentLambda, qMax(0, m_cube.size() - 1));
    const int mn = roiClampedMin();
    const int mx = roiClampedMax();

    const QRect heatRect = m_heatmapLabel->contentsRect().isValid()
        ? m_heatmapLabel->contentsRect()
        : QRect(QPoint(0, 0), m_heatmapLabel->size());
    const QRect spectrumRect = m_spectrumLabel->contentsRect().isValid()
        ? m_spectrumLabel->contentsRect()
        : QRect(QPoint(0, 0), m_spectrumLabel->size());
    const QSize heatLogicalSize = heatRect.size();
    const QSize spectrumLogicalSize = spectrumRect.size();
    const QRect colorbarRect = m_colorbarLabel && m_colorbarLabel->contentsRect().isValid()
        ? m_colorbarLabel->contentsRect()
        : QRect(QPoint(0, 0), m_colorbarLabel ? m_colorbarLabel->size() : QSize(56, 160));

    const qreal heatDpr = m_heatmapLabel->devicePixelRatioF();
    const qreal spectrumDpr = m_spectrumLabel->devicePixelRatioF();
    const qreal colorbarDpr = m_colorbarLabel ? m_colorbarLabel->devicePixelRatioF() : 1.0;

    const QSize heatTargetPx(qMax(1, int(std::round(heatRect.width() * heatDpr))), qMax(1, int(std::round(heatRect.height() * heatDpr))));
    const QSize spectrumTargetPx(qMax(1, int(std::round(spectrumRect.width() * spectrumDpr))), qMax(1, int(std::round(spectrumRect.height() * spectrumDpr))));
    const QSize colorbarTargetPx(qMax(1, int(std::round(colorbarRect.width() * colorbarDpr))), qMax(1, int(std::round(colorbarRect.height() * colorbarDpr))));

    const QString cmap = m_colormapCombo ? m_colormapCombo->currentText() : QStringLiteral("viridis");
    const int nx = m_nx;
    const int ny = m_ny;
    const QPointer<HeatmapWindow> self(this);

    std::thread([self, nx, ny, mn, mx, currentLambda, cmap, heatTargetPx, spectrumTargetPx, colorbarTargetPx, heatLogicalSize, spectrumLogicalSize, heatDpr, spectrumDpr, colorbarDpr]() mutable {
        if (!self) {
            return;
        }

        QVector<QVector<double>> cube;
        QVector<double> slice;
        {
            QMutexLocker lock(&self->m_cubeMutex);
            cube = self->m_cube;
            if (currentLambda >= 0 && currentLambda < cube.size()) {
                slice = cube[currentLambda];
            }
        }

        double dataMin = 0.0;
        double dataMax = 1.0;
        const QImage heatmapImage = buildHeatmapImage(slice, nx, ny, cmap, &dataMin, &dataMax);
        const QImage colorbarImage = buildColorbarImage(dataMin, dataMax, cmap, colorbarTargetPx);
        const QVector<double> mean = computeMeanSpectrum(cube, mn, mx);
        const QImage spectrumImage = buildSpectrumImage(mean, currentLambda, mn, mx, spectrumTargetPx);

        QMetaObject::invokeMethod(self.data(), [self, heatmapImage, colorbarImage, spectrumImage, mean, nx, ny, mn, mx, currentLambda, dataMin, dataMax, heatLogicalSize, spectrumLogicalSize, heatTargetPx, spectrumTargetPx, heatDpr, spectrumDpr, colorbarDpr]() mutable {
            if (!self) {
                return;
            }

            self->m_lastHeatmapImage = heatmapImage;
            self->m_lastHeatmapLambda = currentLambda;
            self->m_lastHeatmapNx = nx;
            self->m_lastHeatmapNy = ny;
            self->m_lastHeatmapTargetSize = heatLogicalSize;

            self->m_lastSpectrumImage = spectrumImage;
            self->m_lastSpectrumRoiMin = mn;
            self->m_lastSpectrumRoiMax = mx;
            self->m_lastSpectrumTargetSize = spectrumLogicalSize;
            self->m_lastSpectrumMean = mean;
            self->m_dataMin = dataMin;
            self->m_dataMax = dataMax;

            if (self->m_heatmapLabel) {
                QPixmap pm = QPixmap::fromImage(heatmapImage);
                if (heatTargetPx.isValid() && !heatTargetPx.isEmpty()) {
                    pm = pm.scaled(heatTargetPx, Qt::KeepAspectRatio, Qt::FastTransformation);
                }
                pm.setDevicePixelRatio(heatDpr);
                self->m_heatmapLabel->setText(QString());
                self->m_heatmapLabel->setPixmap(pm);
            }
            if (self->m_colorbarLabel) {
                QPixmap pm = QPixmap::fromImage(colorbarImage);
                pm.setDevicePixelRatio(colorbarDpr);
                self->m_colorbarLabel->setPixmap(pm);
            }
            if (self->m_spectrumLabel) {
                QPixmap pm = QPixmap::fromImage(spectrumImage);
                pm.setDevicePixelRatio(spectrumDpr);
                self->m_spectrumLabel->setText(QString());
                self->m_spectrumLabel->setPixmap(pm);
            }

            self->m_renderWorkerRunning = false;
            if (self->m_renderPending.exchange(false)) {
                QTimer::singleShot(0, self.data(), [self]() {
                    if (self) {
                        self->scheduleRender();
                    }
                });
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void HeatmapWindow::discoverExposureFiles(const QString& primaryCsvPath)
{
    m_sources.clear();
    m_sources.push_back({QStringLiteral("Primary"), primaryCsvPath});

    const QFileInfo fi(primaryCsvPath);
    const QString base = fi.fileName();
    QString sessionId;
    if (base.startsWith("measurement_") && base.endsWith("_spectra.csv")) {
        sessionId = base;
        sessionId.remove(0, QStringLiteral("measurement_").size());
        sessionId.chop(QStringLiteral("_spectra.csv").size());
    }

    if (!sessionId.isEmpty()) {
        const QDir folder(fi.absolutePath());
        const QDir sessionDir(folder.absoluteFilePath(QStringLiteral("session_%1").arg(sessionId)));
        const QString metaPath = sessionDir.absoluteFilePath("meta.json");

        QFile meta(metaPath);
        if (meta.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(meta.readAll());
            if (doc.isObject()) {
                const auto root = doc.object();
                const auto filesObj = root.value("files").toObject();
                const auto perExp = filesObj.value("per_exposure_files").toObject();

                for (auto it = perExp.begin(); it != perExp.end(); ++it) {
                    bool okF = false;
                    const double expMs = it.key().toDouble(&okF);
                    if (!okF) {
                        continue;
                    }
                    const QString fileName = it.value().toString();
                    if (fileName.isEmpty()) {
                        continue;
                    }
                    const QString path = sessionDir.absoluteFilePath(fileName);
                    if (!QFileInfo::exists(path)) {
                        continue;
                    }
                    m_sources.push_back({QStringLiteral("%1 ms").arg(expMs, 0, 'f', 1), path});
                }
            }
        }
    }

    if (m_exposureCombo) {
        m_exposureCombo->clear();
        // sort: Primary first, then numeric
        std::sort(m_sources.begin(), m_sources.end(), [](const ExposureSource& a, const ExposureSource& b) {
            if (a.label == QStringLiteral("Primary")) return true;
            if (b.label == QStringLiteral("Primary")) return false;
            // try parse
            bool okA = false;
            bool okB = false;
            const double da = a.label.split(' ').first().toDouble(&okA);
            const double db = b.label.split(' ').first().toDouble(&okB);
            if (okA && okB) return da < db;
            return a.label < b.label;
        });

        for (const auto& s : m_sources) {
            m_exposureCombo->addItem(s.label);
        }
        m_exposureCombo->setCurrentIndex(0);
        m_currentCsv = primaryCsvPath;
    }
}

void HeatmapWindow::buildCube()
{
    QMutexLocker lock(&m_cubeMutex);

    // Map coordinate -> index
    QHash<int, int> xToIdx;
    QHash<int, int> yToIdx;
    for (int i = 0; i < m_xValues.size(); ++i) {
        xToIdx.insert(m_xValues[i], i);
    }
    for (int i = 0; i < m_yValues.size(); ++i) {
        yToIdx.insert(m_yValues[i], i);
    }

    m_cube.clear();
    m_cube.resize(m_spectrumLen);
    for (int k = 0; k < m_spectrumLen; ++k) {
        m_cube[k].fill(0.0, m_nx * m_ny);
    }

    for (const auto& p : m_points) {
        int xi = xToIdx.value(p.x, -1);
        const int yi = yToIdx.value(p.y, -1);
        if (xi < 0 || yi < 0) {
            continue;
        }

        for (int k = 0; k < m_spectrumLen; ++k) {
            const double v = (k < p.spectrum.size()) ? p.spectrum[k] : 0.0;
            m_cube[k][yi * m_nx + xi] = v;
        }
    }
}

void HeatmapWindow::onSliderChanged(int value)
{
    m_currentLambda = globalIdxFromRoiIdx(value);
    scheduleRender();
}

int HeatmapWindow::roiMinIndex() const
{
    return m_roiMinSpin ? int(std::round(m_roiMinSpin->value())) : m_currentRoiMin;
}

int HeatmapWindow::roiMaxIndex() const
{
    return m_roiMaxSpin ? int(std::round(m_roiMaxSpin->value())) : m_currentRoiMax;
}

int HeatmapWindow::roiClampedMin() const
{
    const int a = qBound(0, roiMinIndex(), qMax(0, m_spectrumLen - 1));
    const int b = qBound(0, roiMaxIndex(), qMax(0, m_spectrumLen - 1));
    return qMin(a, b);
}

int HeatmapWindow::roiClampedMax() const
{
    const int a = qBound(0, roiMinIndex(), qMax(0, m_spectrumLen - 1));
    const int b = qBound(0, roiMaxIndex(), qMax(0, m_spectrumLen - 1));
    return qMax(a, b);
}

int HeatmapWindow::roiLen() const
{
    if (m_spectrumLen <= 0) return 0;
    return (roiClampedMax() - roiClampedMin()) + 1;
}

int HeatmapWindow::globalIdxFromRoiIdx(int roiIdx) const
{
    const int mn = roiClampedMin();
    const int mx = roiClampedMax();
    if (mx < mn) return qBound(0, mn, qMax(0, m_spectrumLen - 1));
    return qBound(mn, mn + roiIdx, mx);
}

int HeatmapWindow::roiIdxFromGlobalIdx(int globalIdx) const
{
    const int mn = roiClampedMin();
    const int mx = roiClampedMax();
    const int g = qBound(mn, globalIdx, mx);
    return g - mn;
}

void HeatmapWindow::resetRoiToFull()
{
    if (m_spectrumLen <= 0) return;
    if (m_roiMinSpin) {
        const QSignalBlocker b(m_roiMinSpin);
        m_roiMinSpin->setValue(0);
    }
    if (m_roiMaxSpin) {
        const QSignalBlocker b(m_roiMaxSpin);
        m_roiMaxSpin->setValue(qMax(0, m_spectrumLen - 1));
    }
    onRoiChanged();
}

void HeatmapWindow::onRoiChanged()
{
    if (m_spectrumLen <= 0) return;

    // Keep the same global wavelength index (e.g. 190) when ROI changes.
    const int prevGlobalLambda = m_currentLambda;

    const int mn = roiClampedMin();
    const int mx = roiClampedMax();
    m_currentRoiMin = mn;
    m_currentRoiMax = mx;

    if (m_slider) {
        const QSignalBlocker b(m_slider);
        const int newMax = qMax(0, roiLen() - 1);
        m_slider->setMaximum(newMax);

        const int keepGlobal = qBound(mn, prevGlobalLambda, mx);
        const int roiIdx = keepGlobal - mn;
        m_slider->setValue(qBound(0, roiIdx, newMax));
    }

    m_currentLambda = qBound(mn, prevGlobalLambda, mx);
    scheduleRender();
}

void HeatmapWindow::updateHeatmap()
{
    scheduleRender();
    return;

    if (!m_heatmapLabel) {
        return;
    }
    if (m_currentLambda < 0 || m_currentLambda >= m_cube.size()) {
        return;
    }

    const auto& slice = m_cube[m_currentLambda];
    const QRect target = m_heatmapLabel->contentsRect().isValid() ? m_heatmapLabel->contentsRect() : QRect(QPoint(0, 0), m_heatmapLabel->size());
    const QSize targetSize = target.isValid() ? target.size() : QSize();

    if (m_lastHeatmapLambda == m_currentLambda && m_lastHeatmapNx == m_nx && m_lastHeatmapNy == m_ny && m_lastHeatmapTargetSize == targetSize && !m_lastHeatmapImage.isNull()) {
        QPixmap pm = QPixmap::fromImage(m_lastHeatmapImage);
        if (target.isValid() && !target.size().isEmpty()) {
            pm = pm.scaled(target.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        m_heatmapLabel->setPixmap(pm);

        double minV = slice.isEmpty() ? 0.0 : slice[0];
        double maxV = slice.isEmpty() ? 1.0 : slice[0];
        for (double v : slice) {
            minV = qMin(minV, v);
            maxV = qMax(maxV, v);
        }
        if (qFuzzyCompare(minV, maxV)) {
            maxV = minV + 1.0;
        }
        m_dataMin = minV;
        m_dataMax = maxV;
        updateColorbar(minV, maxV);
        return;
    }

    // Determine scale used in makeHeatmapImage (keep consistent)
    double minV = slice.isEmpty() ? 0.0 : slice[0];
    double maxV = slice.isEmpty() ? 1.0 : slice[0];
    for (double v : slice) {
        minV = qMin(minV, v);
        maxV = qMax(maxV, v);
    }
    if (qFuzzyCompare(minV, maxV)) {
        maxV = minV + 1.0;
    }

    const QImage img = makeHeatmapImage(slice, m_nx, m_ny);
    m_lastHeatmapImage = img;
    m_lastHeatmapLambda = m_currentLambda;
    m_lastHeatmapNx = m_nx;
    m_lastHeatmapNy = m_ny;
    m_lastHeatmapTargetSize = targetSize;

    QPixmap pm = QPixmap::fromImage(img);
    if (target.isValid() && !target.size().isEmpty()) {
        pm = pm.scaled(target.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    m_heatmapLabel->setPixmap(pm);

    m_dataMin = minV;
    m_dataMax = maxV;
    updateColorbar(minV, maxV);
}

void HeatmapWindow::updateColorbar(double minV, double maxV)
{
    if (!m_colorbarLabel) {
        return;
    }

    const QRect r = m_colorbarLabel->contentsRect().isValid()
        ? m_colorbarLabel->contentsRect()
        : QRect(QPoint(0, 0), QSize(m_colorbarLabel->width(), m_colorbarLabel->height()));
    const int w = qMax(28, r.width());
    const int h = qMax(160, r.height());

    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(10, 10, 12));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int barW = qMax(14, w / 3);
    const int barX0 = 6;
    const int barX1 = barX0 + barW;
    const int top = 8;
    const int bottom = h - 8;

    // Gradient: top=max, bottom=min
    for (int y = top; y <= bottom; ++y) {
        const double tt = (bottom == top) ? 0.0 : double(y - top) / double(bottom - top);
        const int c = qBound(0, int((1.0 - tt) * 255.0), 255);
        p.setPen(mapColor(c));
        p.drawLine(barX0, y, barX1, y);
    }

    p.setPen(QPen(QColor(200, 200, 210), 1));
    p.drawRect(QRect(barX0, top, barW, bottom - top));

    // Labels
    p.setPen(QColor(220, 220, 230));
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);

    const int textX = barX1 + 6;
    p.drawText(QRect(textX, top - 2, w - textX - 2, 16), Qt::AlignLeft | Qt::AlignTop, QString::number(maxV, 'g', 4));
    p.drawText(QRect(textX, bottom - 14, w - textX - 2, 16), Qt::AlignLeft | Qt::AlignBottom, QString::number(minV, 'g', 4));

    m_colorbarLabel->setPixmap(QPixmap::fromImage(img));
}

void HeatmapWindow::updateSpectrum()
{
    scheduleRender();
    return;

    if (!m_spectrumLabel) {
        return;
    }

    const int mn = roiClampedMin();
    const int mx = roiClampedMax();
    const int len = roiLen();
    if (len <= 0) return;

    const QRect targetRect = m_spectrumLabel->contentsRect().isValid() ? m_spectrumLabel->contentsRect() : QRect(QPoint(0, 0), m_spectrumLabel->size());
    const QSize targetSize = targetRect.isValid() ? targetRect.size() : QSize();
    const qreal dpr = m_spectrumLabel->devicePixelRatioF();
    const int w = qMax(600, int(std::round(targetRect.width() * dpr)));
    const int h = qMax(220, int(std::round(targetRect.height() * dpr)));

    if (m_lastSpectrumRoiMin == mn && m_lastSpectrumRoiMax == mx && m_lastSpectrumTargetSize == targetSize && !m_lastSpectrumImage.isNull() && !m_lastSpectrumMean.isEmpty()) {
        const int lenCached = m_lastSpectrumMean.size();
        const int n = m_nx * m_ny;
        if (lenCached == len && n > 0) {
            const QVector<double> mean = m_lastSpectrumMean;

            QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);

            // Dark background.
            {
                QLinearGradient bg(0, 0, 0, h);
                bg.setColorAt(0.0, QColor(14, 14, 16));
                bg.setColorAt(1.0, QColor(8, 8, 10));
                QPainter bgp(&img);
                bgp.fillRect(QRect(0, 0, w, h), bg);
            }

            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::TextAntialiasing, true);

            const QFont tickFont(QFont(p.font().family(), 8));
            const QFont titleFont(QFont(p.font().family(), 9, QFont::DemiBold));
            const QFontMetrics fmTicks(tickFont);
            const QFontMetrics fmTitle(titleFont);

            const int left = qMax(64, fmTicks.horizontalAdvance(QStringLiteral("0000.0")) + 18);
            const int right = qMax(18, fmTicks.horizontalAdvance(QStringLiteral("000")) / 2);
            const int top = qMax(12, fmTitle.height() + 8);
            const int bottom = qMax(54, fmTicks.height() * 2 + 18);
            const QRect plot(left, top, qMax(10, w - left - right), qMax(10, h - top - bottom));

            double minV = mean[0];
            double maxV = mean[0];
            for (double v : mean) {
                minV = qMin(minV, v);
                maxV = qMax(maxV, v);
            }
            if (qFuzzyCompare(minV, maxV)) {
                maxV = minV + 1.0;
            }
            const double yPad = (maxV - minV) * 0.08;
            minV -= yPad;
            maxV += yPad;

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

            QPolygonF curve;
            curve.reserve(len);
            for (int k = 0; k < len; ++k) {
                const double t = (len == 1) ? 0.0 : double(k) / double(len - 1);
                const qreal x = plot.left() + qreal(t) * qreal(plot.width());
                const double vn = (mean[k] - minV) / (maxV - minV);
                const qreal y = plot.bottom() - qreal(vn) * qreal(plot.height());
                curve.push_back(QPointF(x, y));
            }

            {
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
            }

            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0, 255, 180, 60), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(curve);
            p.setPen(QPen(QColor(0, 230, 150), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(curve);

            if (len > 1) {
                const int roiIx = roiIdxFromGlobalIdx(m_currentLambda);
                const double t = double(roiIx) / double(len - 1);
                const int x = plot.left() + int(std::round(t * plot.width()));
                p.setPen(QPen(QColor(255, 90, 90), 2, Qt::DashLine));
                p.drawLine(QPoint(x, plot.top()), QPoint(x, plot.bottom()));
            }

            p.setPen(QColor(230, 230, 235));
            p.setFont(titleFont);
            p.drawText(QRect(8, 6, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Spectrum"));

            p.setFont(QFont(p.font().family(), 9));
            p.setPen(QColor(165, 165, 175));
            p.drawText(QRect(8, h - 22, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("Wavelength index: %1 (ROI %2..%3)")
                    .arg(m_currentLambda)
                    .arg(mn)
                    .arg(mx));

            p.setFont(tickFont);
            p.setPen(QColor(130, 130, 140));
            p.drawText(QRect(0, plot.top() - 6, left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxV, 'g', 4));
            p.drawText(QRect(0, plot.bottom() - (fmTicks.height() / 2), left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(minV, 'g', 4));

            {
                p.setPen(QColor(130, 130, 140));
                const int ticks = 5;
                const int span = qMax(1, mx - mn);
                for (int i = 0; i <= ticks; ++i) {
                    const double t = double(i) / double(ticks);
                    const int globalIdx = mn + int(std::round(t * span));
                    const int x = plot.left() + int(std::round(t * plot.width()));
                    p.drawLine(QPoint(x, plot.bottom()), QPoint(x, plot.bottom() + 4));

                    const QString s = QString::number(globalIdx);
                    const int tw = fmTicks.horizontalAdvance(s);
                    p.drawText(QRect(x - tw / 2, plot.bottom() + 6, tw + 2, fmTicks.height()), Qt::AlignHCenter | Qt::AlignTop, s);
                }
            }

            p.setPen(QColor(190, 190, 200));
            p.drawText(QRect(plot.left(), plot.bottom() + (fmTicks.height() * 2 + 12), plot.width(), fmTicks.height() + 2), Qt::AlignCenter, QStringLiteral("Wavelength"));
            p.save();
            p.translate(18, plot.top() + plot.height() / 2);
            p.rotate(-90);
            p.drawText(QRect(-plot.height() / 2, -12, plot.height(), 18), Qt::AlignCenter, QStringLiteral("Intensity"));
            p.restore();

            m_lastSpectrumImage = img;
            m_spectrumLabel->setPixmap(QPixmap::fromImage(img));
            return;
        }
    }

    // Mean spectrum in ROI
    QVector<double> mean;
    mean.fill(0.0, len);

    const int n = m_nx * m_ny;
    if (n <= 0) {
        return;
    }

    for (int gk = mn; gk <= mx; ++gk) {
        const auto& slice = m_cube[gk];
        if (slice.isEmpty()) {
            mean[gk - mn] = 0.0;
            continue;
        }

#if SPEKTROMETR_HAS_OPENCV
        try {
            cv::Mat row(1, slice.size(), CV_64F, const_cast<double*>(slice.constData()));
            const cv::Scalar avg = cv::mean(row);
            mean[gk - mn] = avg[0];
        } catch (const cv::Exception&) {
            double sum = 0.0;
            for (int i = 0; i < slice.size(); ++i) {
                sum += slice[i];
            }
            mean[gk - mn] = sum / double(slice.size());
        }
#else
        double sum = 0.0;
        for (int i = 0; i < slice.size(); ++i) {
            sum += slice[i];
        }
        mean[gk - mn] = sum / double(slice.size());
#endif
    }

    m_lastSpectrumMean = mean;
    m_lastSpectrumRoiMin = mn;
    m_lastSpectrumRoiMax = mx;
    m_lastSpectrumTargetSize = targetSize;

    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    // Dark background.
    {
        QLinearGradient bg(0, 0, 0, h);
        bg.setColorAt(0.0, QColor(14, 14, 16));
        bg.setColorAt(1.0, QColor(8, 8, 10));
        QPainter bgp(&img);
        bgp.fillRect(QRect(0, 0, w, h), bg);
    }

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QFont tickFont(QFont(p.font().family(), 8));
    const QFont titleFont(QFont(p.font().family(), 9, QFont::DemiBold));
    const QFontMetrics fmTicks(tickFont);
    const QFontMetrics fmTitle(titleFont);

    const int left = qMax(64, fmTicks.horizontalAdvance(QStringLiteral("0000.0")) + 18);
    const int right = qMax(18, fmTicks.horizontalAdvance(QStringLiteral("000")) / 2);
    const int top = qMax(12, fmTitle.height() + 8);
    const int bottom = qMax(54, fmTicks.height() * 2 + 18);
    const QRect plot(left, top, qMax(10, w - left - right), qMax(10, h - top - bottom));
    // Determine Y range
    double minV = mean[0];
    double maxV = mean[0];
    for (double v : mean) {
        minV = qMin(minV, v);
        maxV = qMax(maxV, v);
    }
    if (qFuzzyCompare(minV, maxV)) {
        maxV = minV + 1.0;
    }
    const double yPad = (maxV - minV) * 0.08;
    minV -= yPad;
    maxV += yPad;

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

    QPolygonF curve;
    curve.reserve(len);
    for (int k = 0; k < len; ++k) {
        const double t = (len == 1) ? 0.0 : double(k) / double(len - 1);
        const qreal x = plot.left() + qreal(t) * qreal(plot.width());
        const double vn = (mean[k] - minV) / (maxV - minV);
        const qreal y = plot.bottom() - qreal(vn) * qreal(plot.height());
        curve.push_back(QPointF(x, y));
    }

    {
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
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 255, 180, 60), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(curve);
    p.setPen(QPen(QColor(0, 230, 150), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(curve);

    // Current lambda indicator
    if (len > 1) {
        const int roiIx = roiIdxFromGlobalIdx(m_currentLambda);
        const double t = double(roiIx) / double(len - 1);
        const int x = plot.left() + int(std::round(t * plot.width()));
        p.setPen(QPen(QColor(255, 90, 90), 2, Qt::DashLine));
        p.drawLine(QPoint(x, plot.top()), QPoint(x, plot.bottom()));
    }

    // Labels
    p.setPen(QColor(230, 230, 235));
    p.setFont(titleFont);
    p.drawText(QRect(8, 6, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Spectrum"));

    p.setFont(QFont(p.font().family(), 9));
    p.setPen(QColor(165, 165, 175));
    p.drawText(QRect(8, h - 22, w - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Wavelength index: %1 (ROI %2..%3)")
            .arg(m_currentLambda)
            .arg(mn)
            .arg(mx));

    // Y labels (min/max)
    p.setFont(tickFont);
    p.setPen(QColor(130, 130, 140));
    p.drawText(QRect(0, plot.top() - 6, left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxV, 'g', 4));
    p.drawText(QRect(0, plot.bottom() - (fmTicks.height() / 2), left - 12, fmTicks.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(minV, 'g', 4));

    // X ticks (global wavelength indices within ROI)
    {
        p.setPen(QColor(130, 130, 140));
        const int ticks = 5;
        const int span = qMax(1, mx - mn);
        for (int i = 0; i <= ticks; ++i) {
            const double t = double(i) / double(ticks);
            const int globalIdx = mn + int(std::round(t * span));
            const int x = plot.left() + int(std::round(t * plot.width()));
            p.drawLine(QPoint(x, plot.bottom()), QPoint(x, plot.bottom() + 4));

            const QString s = QString::number(globalIdx);
            const int tw = fmTicks.horizontalAdvance(s);
            p.drawText(QRect(x - tw / 2, plot.bottom() + 6, tw + 2, fmTicks.height()), Qt::AlignHCenter | Qt::AlignTop, s);
        }
    }

    // Axis labels
    p.setPen(QColor(190, 190, 200));
    p.drawText(QRect(plot.left(), plot.bottom() + (fmTicks.height() * 2 + 12), plot.width(), fmTicks.height() + 2), Qt::AlignCenter, QStringLiteral("Wavelength"));
    p.save();
    p.translate(18, plot.top() + plot.height() / 2);
    p.rotate(-90);
    p.drawText(QRect(-plot.height() / 2, -12, plot.height(), 18), Qt::AlignCenter, QStringLiteral("Intensity"));
    p.restore();

    m_lastSpectrumImage = img;
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    m_spectrumLabel->setPixmap(pm);
}

static void applyScaledPixmap(QLabel* lbl, const QImage& src)
{
    if (!lbl) return;
    if (src.isNull()) {
        lbl->setPixmap(QPixmap());
        return;
    }
    const QRect target = lbl->contentsRect().isValid() ? lbl->contentsRect() : QRect(QPoint(0, 0), lbl->size());
    if (!target.isValid() || target.size().isEmpty()) return;

    const qreal dpr = lbl->devicePixelRatioF();
    const QSize targetPx(qMax(1, int(std::round(target.width() * dpr))), qMax(1, int(std::round(target.height() * dpr))));

    QPixmap pm = QPixmap::fromImage(src);
    // Use Fast (nearest-ish) scaling to keep grid/heatmap crisp during resize.
    // High-quality scale comes from re-rendering at target size.
    QPixmap scaled = pm.scaled(targetPx, Qt::KeepAspectRatio, Qt::FastTransformation);
    scaled.setDevicePixelRatio(dpr);
    lbl->setPixmap(scaled);
}

bool HeatmapWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event && (event->type() == QEvent::Show || event->type() == QEvent::Resize)) {
        if (event->type() == QEvent::Resize) {
            // Debounced re-render on resize.
            if (m_resizeRenderTimer) m_resizeRenderTimer->start();
        } else {
            QTimer::singleShot(0, this, [this]() {
                scheduleRender();
                m_deferredInitialRenderDone = true;
            });
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void HeatmapWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // Try to render immediately with current widget sizes.
    // Then do a deferred render on the next tick when Qt finishes the first layout pass.
    scheduleRender();

    if (!m_deferredInitialRenderDone) {
        QTimer::singleShot(0, this, [this]() {
            scheduleRender();
            m_deferredInitialRenderDone = true;
        });
    }
}

QColor HeatmapWindow::mapColor(int c) const
{
    const QString cmap = m_colormapCombo ? m_colormapCombo->currentText() : QStringLiteral("gray");
    if (cmap == QStringLiteral("gray")) {
        return QColor(c, c, c);
    }
    if (cmap == QStringLiteral("hot")) {
        const int r = qMin(255, c * 2);
        const int g = (c > 128) ? qMin(255, (c - 128) * 2) : 0;
        const int b = 0;
        return QColor(r, g, b);
    }
    if (cmap == QStringLiteral("jet")) {
        const double tt = double(c) / 255.0;
        const int r = qBound(0, int(255.0 * qMin(1.0, qMax(0.0, 1.5 - qAbs(4.0 * tt - 3.0)))), 255);
        const int g = qBound(0, int(255.0 * qMin(1.0, qMax(0.0, 1.5 - qAbs(4.0 * tt - 2.0)))), 255);
        const int b = qBound(0, int(255.0 * qMin(1.0, qMax(0.0, 1.5 - qAbs(4.0 * tt - 1.0)))), 255);
        return QColor(r, g, b);
    }
    // viridis (simple 5-point LUT)
    static const QColor lut[] = {
        QColor(68, 1, 84), QColor(59, 82, 139), QColor(33, 145, 140), QColor(94, 201, 98), QColor(253, 231, 37)
    };
    const double tt = double(c) / 255.0;
    const int segs = 4;
    const double pos = tt * segs;
    const int i0 = qBound(0, int(pos), segs - 1);
    const int i1 = i0 + 1;
    const double a = pos - double(i0);
    const QColor c0 = lut[i0];
    const QColor c1 = lut[i1];
    const int r = int(c0.red() + a * (c1.red() - c0.red()));
    const int g = int(c0.green() + a * (c1.green() - c0.green()));
    const int b = int(c0.blue() + a * (c1.blue() - c0.blue()));
    return QColor(r, g, b);
}

QImage HeatmapWindow::makeHeatmapImage(const QVector<double>& slice, int nx, int ny)
{
    // Autoscale per current slice (simple)
    double minV = slice.isEmpty() ? 0.0 : slice[0];
    double maxV = slice.isEmpty() ? 1.0 : slice[0];
    for (double v : slice) {
        minV = qMin(minV, v);
        maxV = qMax(maxV, v);
    }

    if (qFuzzyCompare(minV, maxV)) {
        maxV = minV + 1.0;
    }

    QImage img(nx, ny, QImage::Format_ARGB32_Premultiplied);

    // Show Y growing upwards (typical stage coordinates): row 0 at bottom.
    for (int y = 0; y < ny; ++y) {
        const int yy = (ny - 1) - y;
        for (int x = 0; x < nx; ++x) {
            const double v = slice[y * nx + x];
            const double t = (v - minV) / (maxV - minV);
            const int c = qBound(0, int(t * 255.0), 255);
            img.setPixelColor(x, yy, mapColor(c));
        }
    }

    return img;
}

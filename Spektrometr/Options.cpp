#include "Options.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

Options Options::defaults()
{
    return Options();
}

static int getInt(const QJsonObject& o, const char* key, int def)
{
    const auto v = o.value(QString::fromLatin1(key));
    if (v.isDouble()) {
        return int(v.toDouble());
    }
    if (v.isString()) {
        bool ok = false;
        const int i = v.toString().toInt(&ok);
        return ok ? i : def;
    }
    return def;
}

static double getDouble(const QJsonObject& o, const char* key, double def)
{
    const auto v = o.value(QString::fromLatin1(key));
    if (v.isDouble()) {
        return v.toDouble();
    }
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        return ok ? d : def;
    }
    return def;
}

static bool getBool(const QJsonObject& o, const char* key, bool def)
{
    const auto v = o.value(QString::fromLatin1(key));
    if (v.isBool()) {
        return v.toBool();
    }
    if (v.isDouble()) {
        return v.toInt() != 0;
    }
    if (v.isString()) {
        const QString s = v.toString().trimmed().toLower();
        if (s == QStringLiteral("true") || s == QStringLiteral("1") || s == QStringLiteral("yes") || s == QStringLiteral("on")) {
            return true;
        }
        if (s == QStringLiteral("false") || s == QStringLiteral("0") || s == QStringLiteral("no") || s == QStringLiteral("off")) {
            return false;
        }
    }
    return def;
}

static QString getString(const QJsonObject& o, const char* key, const QString& def)
{
    const auto v = o.value(QString::fromLatin1(key));
    if (v.isString()) {
        return v.toString();
    }
    if (v.isDouble()) {
        return QString::number(v.toDouble());
    }
    return def;
}

Options loadOptions(const QString& path)
{
    Options opt = Options::defaults();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return opt;
    }

    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return opt;
    }

    const auto o = doc.object();

    opt.step_x = getInt(o, "step_x", opt.step_x);
    opt.step_y = getInt(o, "step_y", opt.step_y);
    opt.width = getInt(o, "width", opt.width);
    opt.height = getInt(o, "height", opt.height);
    opt.await_s = getDouble(o, "await", opt.await_s);
    opt.sequence_sleep_s = getDouble(o, "sequence_sleep", opt.sequence_sleep_s);

    opt.starting_corner = getString(o, "starting_corner", opt.starting_corner);

    opt.xmin = getString(o, "xmin", opt.xmin);
    opt.xmax = getString(o, "xmax", opt.xmax);

    opt.port_x = getString(o, "port_x", opt.port_x);
    opt.port_y = getString(o, "port_y", opt.port_y);

    opt.camera_index = getInt(o, "camera_index", opt.camera_index);

    opt.exposure_time_ms = getDouble(o, "exposure_time", opt.exposure_time_ms);
    opt.gain = getDouble(o, "gain", opt.gain);

    opt.sequence_exposure_times = getString(o, "sequence_exposure_times", opt.sequence_exposure_times);

    opt.spectrum_range_min = getDouble(o, "spectrum_range_min", opt.spectrum_range_min);
    opt.spectrum_range_max = getDouble(o, "spectrum_range_max", opt.spectrum_range_max);

    opt.spectrum_cal_pixel1 = getDouble(o, "spectrum_cal_pixel1", opt.spectrum_cal_pixel1);
    opt.spectrum_cal_nm1 = getDouble(o, "spectrum_cal_nm1", opt.spectrum_cal_nm1);
    opt.spectrum_cal_pixel2 = getDouble(o, "spectrum_cal_pixel2", opt.spectrum_cal_pixel2);
    opt.spectrum_cal_nm2 = getDouble(o, "spectrum_cal_nm2", opt.spectrum_cal_nm2);

    return opt;
}

bool saveOptions(const QString& path, const Options& opt)
{
    QJsonObject o;
    o.insert(QStringLiteral("step_x"), opt.step_x);
    o.insert(QStringLiteral("step_y"), opt.step_y);
    o.insert(QStringLiteral("width"), opt.width);
    o.insert(QStringLiteral("height"), opt.height);
    o.insert(QStringLiteral("await"), opt.await_s);
    o.insert(QStringLiteral("sequence_sleep"), opt.sequence_sleep_s);

    o.insert(QStringLiteral("starting_corner"), opt.starting_corner);

    o.insert(QStringLiteral("xmin"), opt.xmin);
    o.insert(QStringLiteral("xmax"), opt.xmax);

    o.insert(QStringLiteral("port_x"), opt.port_x);
    o.insert(QStringLiteral("port_y"), opt.port_y);

    o.insert(QStringLiteral("camera_index"), opt.camera_index);

    o.insert(QStringLiteral("exposure_time"), opt.exposure_time_ms);
    o.insert(QStringLiteral("gain"), opt.gain);

    o.insert(QStringLiteral("sequence_exposure_times"), opt.sequence_exposure_times);

    o.insert(QStringLiteral("spectrum_range_min"), opt.spectrum_range_min);
    o.insert(QStringLiteral("spectrum_range_max"), opt.spectrum_range_max);

    o.insert(QStringLiteral("spectrum_cal_pixel1"), opt.spectrum_cal_pixel1);
    o.insert(QStringLiteral("spectrum_cal_nm1"), opt.spectrum_cal_nm1);
    o.insert(QStringLiteral("spectrum_cal_pixel2"), opt.spectrum_cal_pixel2);
    o.insert(QStringLiteral("spectrum_cal_nm2"), opt.spectrum_cal_nm2);

    QJsonDocument doc(o);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

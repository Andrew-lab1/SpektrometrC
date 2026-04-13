#pragma once

#include <QString>

struct Options
{
    int step_x = 20;
    int step_y = 20;
    int width = 200;
    int height = 200;
    double await_s = 0.01;
    double sequence_sleep_s = 0.1;

    QString starting_corner = QStringLiteral("top-left");

    QString xmin = QStringLiteral("0");
    QString xmax = QStringLiteral("2048");

    QString port_x = QStringLiteral("COM5");
    QString port_y = QStringLiteral("COM9");

    int camera_index = 0;

    double exposure_time_ms = 10.0;
    double gain = 1.0;

    QString sequence_exposure_times = QString();

    QString results_folder_path = QStringLiteral("measurement_data");

    double spectrum_range_min = 0.0;
    double spectrum_range_max = 2048.0;

    // Simple linear wavelength calibration: two reference points.
    // nm = a * pixel + b, computed from (pixel1,nm1) and (pixel2,nm2).
    // Defaults keep 1:1 mapping so the UI works even without calibration.
    double spectrum_cal_pixel1 = 0.0;
    double spectrum_cal_nm1 = 0.0;
    double spectrum_cal_pixel2 = 2048.0;
    double spectrum_cal_nm2 = 2048.0;

    static Options defaults();
};

Options loadOptions(const QString& path);
bool saveOptions(const QString& path, const Options& opt);

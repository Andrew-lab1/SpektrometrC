/********************************************************************************
** Form generated from reading UI file 'Spektrometr.ui'
**
** Created by: Qt User Interface Compiler version 6.10.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SPEKTROMETR_H
#define UI_SPEKTROMETR_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SpektrometrClass
{
public:
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout;
    QTabWidget *tabWidget;
    QWidget *tabSpectrum;
    QVBoxLayout *verticalLayoutSpectrum;
    QFrame *frameCameraPreview;
    QVBoxLayout *verticalLayoutFrameCamera;
    QGridLayout *gridLayoutCameraOverlay;
    QLabel *labelPixelinkPreview;
    QFrame *overlayMotorControls;
    QVBoxLayout *verticalLayoutOverlay;
    QHBoxLayout *horizontalLayoutMotorStep;
    QLabel *labelMotorStep;
    QSpinBox *spinMotorStep;
    QSpacerItem *horizontalSpacerMotor;
    QGridLayout *gridLayoutMotorButtons;
    QPushButton *btnMotorUp;
    QPushButton *btnMotorLeft;
    QPushButton *btnMotorHome;
    QPushButton *btnMotorRight;
    QPushButton *btnMotorDown;
    QSpacerItem *verticalSpacerOverlay;
    QVBoxLayout *verticalLayoutSpectrumBottom;
    QHBoxLayout *horizontalLayoutCameraButtons;
    QPushButton *btnStartSequence;
    QPushButton *btnStopSequence;
    QHBoxLayout *horizontalLayoutSpectrumControls;
    QLabel *labelRoi;
    QDoubleSpinBox *spinRoiMin;
    QLabel *labelRoiTo;
    QDoubleSpinBox *spinRoiMax;
    QPushButton *btnApplyRoi;
    QPushButton *btnResetRoi;
    QLabel *labelExposureSpectrum;
    QDoubleSpinBox *spinExposureSpectrum;
    QLabel *labelGainSpectrum;
    QDoubleSpinBox *spinGainSpectrum;
    QSpacerItem *horizontalSpacerSpectrum;
    QHBoxLayout *horizontalLayoutExposureSequence;
    QLabel *labelExposureSequence;
    QLineEdit *editExposureSequence;
    QLabel *labelSpectrumPlaceholder;
    QWidget *tabResults;
    QVBoxLayout *verticalLayoutResults;
    QHBoxLayout *horizontalLayoutResultsButtons;
    QPushButton *btnRefreshResults;
    QPushButton *btnExportAll;
    QPushButton *btnOpenMeasurement;
    QPushButton *btnDeleteSelected;
    QPushButton *btnDeleteAll;
    QSpacerItem *horizontalSpacer;
    QLabel *labelResultsInfo;
    QListWidget *listMeasurements;
    QWidget *tabSettings;
    QFormLayout *formLayoutSettings;
    QLabel *labelScanWidth;
    QSpinBox *spinScanWidth;
    QLabel *labelScanHeight;
    QSpinBox *spinScanHeight;
    QLabel *labelStepX;
    QSpinBox *spinStepX;
    QLabel *labelStepY;
    QSpinBox *spinStepY;
    QLabel *labelPortX;
    QComboBox *comboPortX;
    QLabel *labelPortY;
    QComboBox *comboPortY;
    QLabel *labelResultsFolderPath;
    QWidget *widgetResultsFolderPath;
    QHBoxLayout *horizontalLayoutResultsFolderPath;
    QLineEdit *editResultsFolderPath;
    QPushButton *btnBrowseResultsFolder;
    QPushButton *btnSaveSettings;
    QMenuBar *menuBar;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *SpektrometrClass)
    {
        if (SpektrometrClass->objectName().isEmpty())
            SpektrometrClass->setObjectName("SpektrometrClass");
        SpektrometrClass->resize(750, 732);
        centralWidget = new QWidget(SpektrometrClass);
        centralWidget->setObjectName("centralWidget");
        verticalLayout = new QVBoxLayout(centralWidget);
        verticalLayout->setSpacing(6);
        verticalLayout->setContentsMargins(11, 11, 11, 11);
        verticalLayout->setObjectName("verticalLayout");
        tabWidget = new QTabWidget(centralWidget);
        tabWidget->setObjectName("tabWidget");
        tabSpectrum = new QWidget();
        tabSpectrum->setObjectName("tabSpectrum");
        verticalLayoutSpectrum = new QVBoxLayout(tabSpectrum);
        verticalLayoutSpectrum->setSpacing(6);
        verticalLayoutSpectrum->setContentsMargins(11, 11, 11, 11);
        verticalLayoutSpectrum->setObjectName("verticalLayoutSpectrum");
        frameCameraPreview = new QFrame(tabSpectrum);
        frameCameraPreview->setObjectName("frameCameraPreview");
        frameCameraPreview->setMinimumSize(QSize(320, 240));
        frameCameraPreview->setFrameShape(QFrame::Shape::StyledPanel);
        verticalLayoutFrameCamera = new QVBoxLayout(frameCameraPreview);
        verticalLayoutFrameCamera->setSpacing(6);
        verticalLayoutFrameCamera->setContentsMargins(11, 11, 11, 11);
        verticalLayoutFrameCamera->setObjectName("verticalLayoutFrameCamera");
        verticalLayoutFrameCamera->setContentsMargins(0, 0, 0, 0);
        gridLayoutCameraOverlay = new QGridLayout();
        gridLayoutCameraOverlay->setSpacing(0);
        gridLayoutCameraOverlay->setObjectName("gridLayoutCameraOverlay");
        gridLayoutCameraOverlay->setContentsMargins(0, 0, 0, 0);
        labelPixelinkPreview = new QLabel(frameCameraPreview);
        labelPixelinkPreview->setObjectName("labelPixelinkPreview");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(labelPixelinkPreview->sizePolicy().hasHeightForWidth());
        labelPixelinkPreview->setSizePolicy(sizePolicy);
        labelPixelinkPreview->setMinimumSize(QSize(320, 240));
        labelPixelinkPreview->setMaximumSize(QSize(16777215, 16777215));
        labelPixelinkPreview->setFrameShape(QFrame::Shape::NoFrame);
        labelPixelinkPreview->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayoutCameraOverlay->addWidget(labelPixelinkPreview, 0, 0, 1, 1);

        overlayMotorControls = new QFrame(frameCameraPreview);
        overlayMotorControls->setObjectName("overlayMotorControls");
        overlayMotorControls->setMaximumSize(QSize(220, 170));
        overlayMotorControls->setStyleSheet(QString::fromUtf8("QFrame{background:rgba(10,10,12,190);border:1px solid rgba(255,255,255,30);border-radius:6px;} QLabel{color:#e6e6e6;} QPushButton{min-width:28px;min-height:24px;}"));
        verticalLayoutOverlay = new QVBoxLayout(overlayMotorControls);
        verticalLayoutOverlay->setSpacing(6);
        verticalLayoutOverlay->setContentsMargins(11, 11, 11, 11);
        verticalLayoutOverlay->setObjectName("verticalLayoutOverlay");
        verticalLayoutOverlay->setContentsMargins(6, 6, 6, 6);
        horizontalLayoutMotorStep = new QHBoxLayout();
        horizontalLayoutMotorStep->setSpacing(6);
        horizontalLayoutMotorStep->setObjectName("horizontalLayoutMotorStep");
        labelMotorStep = new QLabel(overlayMotorControls);
        labelMotorStep->setObjectName("labelMotorStep");

        horizontalLayoutMotorStep->addWidget(labelMotorStep);

        spinMotorStep = new QSpinBox(overlayMotorControls);
        spinMotorStep->setObjectName("spinMotorStep");
        spinMotorStep->setMinimum(1);
        spinMotorStep->setMaximum(100000);
        spinMotorStep->setValue(20);

        horizontalLayoutMotorStep->addWidget(spinMotorStep);

        horizontalSpacerMotor = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayoutMotorStep->addItem(horizontalSpacerMotor);


        verticalLayoutOverlay->addLayout(horizontalLayoutMotorStep);

        gridLayoutMotorButtons = new QGridLayout();
        gridLayoutMotorButtons->setSpacing(6);
        gridLayoutMotorButtons->setObjectName("gridLayoutMotorButtons");
        btnMotorUp = new QPushButton(overlayMotorControls);
        btnMotorUp->setObjectName("btnMotorUp");

        gridLayoutMotorButtons->addWidget(btnMotorUp, 0, 1, 1, 1);

        btnMotorLeft = new QPushButton(overlayMotorControls);
        btnMotorLeft->setObjectName("btnMotorLeft");

        gridLayoutMotorButtons->addWidget(btnMotorLeft, 1, 0, 1, 1);

        btnMotorHome = new QPushButton(overlayMotorControls);
        btnMotorHome->setObjectName("btnMotorHome");

        gridLayoutMotorButtons->addWidget(btnMotorHome, 1, 1, 1, 1);

        btnMotorRight = new QPushButton(overlayMotorControls);
        btnMotorRight->setObjectName("btnMotorRight");

        gridLayoutMotorButtons->addWidget(btnMotorRight, 1, 2, 1, 1);

        btnMotorDown = new QPushButton(overlayMotorControls);
        btnMotorDown->setObjectName("btnMotorDown");

        gridLayoutMotorButtons->addWidget(btnMotorDown, 2, 1, 1, 1);


        verticalLayoutOverlay->addLayout(gridLayoutMotorButtons);

        verticalSpacerOverlay = new QSpacerItem(20, 20, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayoutOverlay->addItem(verticalSpacerOverlay);


        gridLayoutCameraOverlay->addWidget(overlayMotorControls, 0, 0, 1, 1, Qt::AlignmentFlag::AlignLeft|Qt::AlignmentFlag::AlignTop);


        verticalLayoutFrameCamera->addLayout(gridLayoutCameraOverlay);


        verticalLayoutSpectrum->addWidget(frameCameraPreview);

        verticalLayoutSpectrumBottom = new QVBoxLayout();
        verticalLayoutSpectrumBottom->setSpacing(6);
        verticalLayoutSpectrumBottom->setObjectName("verticalLayoutSpectrumBottom");
        verticalLayoutSpectrumBottom->setContentsMargins(0, 0, 0, 0);
        horizontalLayoutCameraButtons = new QHBoxLayout();
        horizontalLayoutCameraButtons->setSpacing(6);
        horizontalLayoutCameraButtons->setObjectName("horizontalLayoutCameraButtons");
        btnStartSequence = new QPushButton(tabSpectrum);
        btnStartSequence->setObjectName("btnStartSequence");

        horizontalLayoutCameraButtons->addWidget(btnStartSequence);

        btnStopSequence = new QPushButton(tabSpectrum);
        btnStopSequence->setObjectName("btnStopSequence");
        btnStopSequence->setEnabled(false);

        horizontalLayoutCameraButtons->addWidget(btnStopSequence);


        verticalLayoutSpectrumBottom->addLayout(horizontalLayoutCameraButtons);

        horizontalLayoutSpectrumControls = new QHBoxLayout();
        horizontalLayoutSpectrumControls->setSpacing(6);
        horizontalLayoutSpectrumControls->setObjectName("horizontalLayoutSpectrumControls");
        labelRoi = new QLabel(tabSpectrum);
        labelRoi->setObjectName("labelRoi");

        horizontalLayoutSpectrumControls->addWidget(labelRoi);

        spinRoiMin = new QDoubleSpinBox(tabSpectrum);
        spinRoiMin->setObjectName("spinRoiMin");
        spinRoiMin->setDecimals(1);
        spinRoiMin->setMaximum(999999.000000000000000);
        spinRoiMin->setValue(0.000000000000000);

        horizontalLayoutSpectrumControls->addWidget(spinRoiMin);

        labelRoiTo = new QLabel(tabSpectrum);
        labelRoiTo->setObjectName("labelRoiTo");

        horizontalLayoutSpectrumControls->addWidget(labelRoiTo);

        spinRoiMax = new QDoubleSpinBox(tabSpectrum);
        spinRoiMax->setObjectName("spinRoiMax");
        spinRoiMax->setDecimals(1);
        spinRoiMax->setMaximum(999999.000000000000000);
        spinRoiMax->setValue(2048.000000000000000);

        horizontalLayoutSpectrumControls->addWidget(spinRoiMax);

        btnApplyRoi = new QPushButton(tabSpectrum);
        btnApplyRoi->setObjectName("btnApplyRoi");

        horizontalLayoutSpectrumControls->addWidget(btnApplyRoi);

        btnResetRoi = new QPushButton(tabSpectrum);
        btnResetRoi->setObjectName("btnResetRoi");

        horizontalLayoutSpectrumControls->addWidget(btnResetRoi);

        labelExposureSpectrum = new QLabel(tabSpectrum);
        labelExposureSpectrum->setObjectName("labelExposureSpectrum");

        horizontalLayoutSpectrumControls->addWidget(labelExposureSpectrum);

        spinExposureSpectrum = new QDoubleSpinBox(tabSpectrum);
        spinExposureSpectrum->setObjectName("spinExposureSpectrum");
        spinExposureSpectrum->setDecimals(1);
        spinExposureSpectrum->setMinimum(0.100000000000000);
        spinExposureSpectrum->setMaximum(5000.000000000000000);
        spinExposureSpectrum->setValue(10.000000000000000);

        horizontalLayoutSpectrumControls->addWidget(spinExposureSpectrum);

        labelGainSpectrum = new QLabel(tabSpectrum);
        labelGainSpectrum->setObjectName("labelGainSpectrum");

        horizontalLayoutSpectrumControls->addWidget(labelGainSpectrum);

        spinGainSpectrum = new QDoubleSpinBox(tabSpectrum);
        spinGainSpectrum->setObjectName("spinGainSpectrum");
        spinGainSpectrum->setDecimals(1);
        spinGainSpectrum->setMinimum(1.000000000000000);
        spinGainSpectrum->setMaximum(10.000000000000000);
        spinGainSpectrum->setSingleStep(0.100000000000000);
        spinGainSpectrum->setValue(1.000000000000000);

        horizontalLayoutSpectrumControls->addWidget(spinGainSpectrum);

        horizontalSpacerSpectrum = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayoutSpectrumControls->addItem(horizontalSpacerSpectrum);


        verticalLayoutSpectrumBottom->addLayout(horizontalLayoutSpectrumControls);

        horizontalLayoutExposureSequence = new QHBoxLayout();
        horizontalLayoutExposureSequence->setSpacing(6);
        horizontalLayoutExposureSequence->setObjectName("horizontalLayoutExposureSequence");
        labelExposureSequence = new QLabel(tabSpectrum);
        labelExposureSequence->setObjectName("labelExposureSequence");

        horizontalLayoutExposureSequence->addWidget(labelExposureSequence);

        editExposureSequence = new QLineEdit(tabSpectrum);
        editExposureSequence->setObjectName("editExposureSequence");

        horizontalLayoutExposureSequence->addWidget(editExposureSequence);


        verticalLayoutSpectrumBottom->addLayout(horizontalLayoutExposureSequence);

        labelSpectrumPlaceholder = new QLabel(tabSpectrum);
        labelSpectrumPlaceholder->setObjectName("labelSpectrumPlaceholder");
        sizePolicy.setHeightForWidth(labelSpectrumPlaceholder->sizePolicy().hasHeightForWidth());
        labelSpectrumPlaceholder->setSizePolicy(sizePolicy);
        labelSpectrumPlaceholder->setMinimumSize(QSize(0, 260));
        labelSpectrumPlaceholder->setFrameShape(QFrame::Shape::StyledPanel);
        labelSpectrumPlaceholder->setScaledContents(false);
        labelSpectrumPlaceholder->setAlignment(Qt::AlignmentFlag::AlignCenter);

        verticalLayoutSpectrumBottom->addWidget(labelSpectrumPlaceholder);


        verticalLayoutSpectrum->addLayout(verticalLayoutSpectrumBottom);

        verticalLayoutSpectrum->setStretch(0, 3);
        verticalLayoutSpectrum->setStretch(1, 2);
        tabWidget->addTab(tabSpectrum, QString());
        tabResults = new QWidget();
        tabResults->setObjectName("tabResults");
        verticalLayoutResults = new QVBoxLayout(tabResults);
        verticalLayoutResults->setSpacing(6);
        verticalLayoutResults->setContentsMargins(11, 11, 11, 11);
        verticalLayoutResults->setObjectName("verticalLayoutResults");
        horizontalLayoutResultsButtons = new QHBoxLayout();
        horizontalLayoutResultsButtons->setSpacing(6);
        horizontalLayoutResultsButtons->setObjectName("horizontalLayoutResultsButtons");
        btnRefreshResults = new QPushButton(tabResults);
        btnRefreshResults->setObjectName("btnRefreshResults");

        horizontalLayoutResultsButtons->addWidget(btnRefreshResults);

        btnExportAll = new QPushButton(tabResults);
        btnExportAll->setObjectName("btnExportAll");

        horizontalLayoutResultsButtons->addWidget(btnExportAll);

        btnOpenMeasurement = new QPushButton(tabResults);
        btnOpenMeasurement->setObjectName("btnOpenMeasurement");

        horizontalLayoutResultsButtons->addWidget(btnOpenMeasurement);

        btnDeleteSelected = new QPushButton(tabResults);
        btnDeleteSelected->setObjectName("btnDeleteSelected");

        horizontalLayoutResultsButtons->addWidget(btnDeleteSelected);

        btnDeleteAll = new QPushButton(tabResults);
        btnDeleteAll->setObjectName("btnDeleteAll");

        horizontalLayoutResultsButtons->addWidget(btnDeleteAll);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayoutResultsButtons->addItem(horizontalSpacer);

        labelResultsInfo = new QLabel(tabResults);
        labelResultsInfo->setObjectName("labelResultsInfo");

        horizontalLayoutResultsButtons->addWidget(labelResultsInfo);


        verticalLayoutResults->addLayout(horizontalLayoutResultsButtons);

        listMeasurements = new QListWidget(tabResults);
        listMeasurements->setObjectName("listMeasurements");

        verticalLayoutResults->addWidget(listMeasurements);

        tabWidget->addTab(tabResults, QString());
        tabSettings = new QWidget();
        tabSettings->setObjectName("tabSettings");
        formLayoutSettings = new QFormLayout(tabSettings);
        formLayoutSettings->setSpacing(6);
        formLayoutSettings->setContentsMargins(11, 11, 11, 11);
        formLayoutSettings->setObjectName("formLayoutSettings");
        labelScanWidth = new QLabel(tabSettings);
        labelScanWidth->setObjectName("labelScanWidth");

        formLayoutSettings->setWidget(0, QFormLayout::ItemRole::LabelRole, labelScanWidth);

        spinScanWidth = new QSpinBox(tabSettings);
        spinScanWidth->setObjectName("spinScanWidth");
        spinScanWidth->setMaximum(1000000);
        spinScanWidth->setValue(200);

        formLayoutSettings->setWidget(0, QFormLayout::ItemRole::FieldRole, spinScanWidth);

        labelScanHeight = new QLabel(tabSettings);
        labelScanHeight->setObjectName("labelScanHeight");

        formLayoutSettings->setWidget(1, QFormLayout::ItemRole::LabelRole, labelScanHeight);

        spinScanHeight = new QSpinBox(tabSettings);
        spinScanHeight->setObjectName("spinScanHeight");
        spinScanHeight->setMaximum(1000000);
        spinScanHeight->setValue(200);

        formLayoutSettings->setWidget(1, QFormLayout::ItemRole::FieldRole, spinScanHeight);

        labelStepX = new QLabel(tabSettings);
        labelStepX->setObjectName("labelStepX");

        formLayoutSettings->setWidget(2, QFormLayout::ItemRole::LabelRole, labelStepX);

        spinStepX = new QSpinBox(tabSettings);
        spinStepX->setObjectName("spinStepX");
        spinStepX->setMaximum(100000);
        spinStepX->setValue(20);

        formLayoutSettings->setWidget(2, QFormLayout::ItemRole::FieldRole, spinStepX);

        labelStepY = new QLabel(tabSettings);
        labelStepY->setObjectName("labelStepY");

        formLayoutSettings->setWidget(3, QFormLayout::ItemRole::LabelRole, labelStepY);

        spinStepY = new QSpinBox(tabSettings);
        spinStepY->setObjectName("spinStepY");
        spinStepY->setMaximum(100000);
        spinStepY->setValue(20);

        formLayoutSettings->setWidget(3, QFormLayout::ItemRole::FieldRole, spinStepY);

        labelPortX = new QLabel(tabSettings);
        labelPortX->setObjectName("labelPortX");

        formLayoutSettings->setWidget(4, QFormLayout::ItemRole::LabelRole, labelPortX);

        comboPortX = new QComboBox(tabSettings);
        comboPortX->setObjectName("comboPortX");

        formLayoutSettings->setWidget(4, QFormLayout::ItemRole::FieldRole, comboPortX);

        labelPortY = new QLabel(tabSettings);
        labelPortY->setObjectName("labelPortY");

        formLayoutSettings->setWidget(5, QFormLayout::ItemRole::LabelRole, labelPortY);

        comboPortY = new QComboBox(tabSettings);
        comboPortY->setObjectName("comboPortY");

        formLayoutSettings->setWidget(5, QFormLayout::ItemRole::FieldRole, comboPortY);

        labelResultsFolderPath = new QLabel(tabSettings);
        labelResultsFolderPath->setObjectName("labelResultsFolderPath");

        formLayoutSettings->setWidget(6, QFormLayout::ItemRole::LabelRole, labelResultsFolderPath);

        widgetResultsFolderPath = new QWidget(tabSettings);
        widgetResultsFolderPath->setObjectName("widgetResultsFolderPath");
        horizontalLayoutResultsFolderPath = new QHBoxLayout(widgetResultsFolderPath);
        horizontalLayoutResultsFolderPath->setSpacing(6);
        horizontalLayoutResultsFolderPath->setContentsMargins(11, 11, 11, 11);
        horizontalLayoutResultsFolderPath->setObjectName("horizontalLayoutResultsFolderPath");
        horizontalLayoutResultsFolderPath->setContentsMargins(0, 0, 0, 0);
        editResultsFolderPath = new QLineEdit(widgetResultsFolderPath);
        editResultsFolderPath->setObjectName("editResultsFolderPath");

        horizontalLayoutResultsFolderPath->addWidget(editResultsFolderPath);

        btnBrowseResultsFolder = new QPushButton(widgetResultsFolderPath);
        btnBrowseResultsFolder->setObjectName("btnBrowseResultsFolder");

        horizontalLayoutResultsFolderPath->addWidget(btnBrowseResultsFolder);


        formLayoutSettings->setWidget(6, QFormLayout::ItemRole::FieldRole, widgetResultsFolderPath);

        btnSaveSettings = new QPushButton(tabSettings);
        btnSaveSettings->setObjectName("btnSaveSettings");

        formLayoutSettings->setWidget(7, QFormLayout::ItemRole::FieldRole, btnSaveSettings);

        tabWidget->addTab(tabSettings, QString());

        verticalLayout->addWidget(tabWidget);

        SpektrometrClass->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(SpektrometrClass);
        menuBar->setObjectName("menuBar");
        menuBar->setGeometry(QRect(0, 0, 750, 21));
        SpektrometrClass->setMenuBar(menuBar);
        statusBar = new QStatusBar(SpektrometrClass);
        statusBar->setObjectName("statusBar");
        SpektrometrClass->setStatusBar(statusBar);

        retranslateUi(SpektrometrClass);

        QMetaObject::connectSlotsByName(SpektrometrClass);
    } // setupUi

    void retranslateUi(QMainWindow *SpektrometrClass)
    {
        SpektrometrClass->setWindowTitle(QCoreApplication::translate("SpektrometrClass", "Spektrometr", nullptr));
        labelPixelinkPreview->setText(QCoreApplication::translate("SpektrometrClass", "PixeLink preview", nullptr));
        labelMotorStep->setText(QCoreApplication::translate("SpektrometrClass", "Step (um):", nullptr));
        btnMotorUp->setText(QCoreApplication::translate("SpektrometrClass", "\342\206\221", nullptr));
        btnMotorLeft->setText(QCoreApplication::translate("SpektrometrClass", "\342\206\220", nullptr));
        btnMotorHome->setText(QCoreApplication::translate("SpektrometrClass", "\342\214\202", nullptr));
        btnMotorRight->setText(QCoreApplication::translate("SpektrometrClass", "\342\206\222", nullptr));
        btnMotorDown->setText(QCoreApplication::translate("SpektrometrClass", "\342\206\223", nullptr));
        btnStartSequence->setText(QCoreApplication::translate("SpektrometrClass", "Start sequence", nullptr));
        btnStopSequence->setText(QCoreApplication::translate("SpektrometrClass", "Stop sequence", nullptr));
        labelRoi->setText(QCoreApplication::translate("SpektrometrClass", "ROI:", nullptr));
        labelRoiTo->setText(QCoreApplication::translate("SpektrometrClass", "to", nullptr));
        btnApplyRoi->setText(QCoreApplication::translate("SpektrometrClass", "Apply ROI", nullptr));
        btnResetRoi->setText(QCoreApplication::translate("SpektrometrClass", "Reset", nullptr));
        labelExposureSpectrum->setText(QCoreApplication::translate("SpektrometrClass", "Exposure (ms) [0.1..5000]", nullptr));
        labelGainSpectrum->setText(QCoreApplication::translate("SpektrometrClass", "Gain [1..10]", nullptr));
        labelExposureSequence->setText(QCoreApplication::translate("SpektrometrClass", "Exposure sequence (ms;...):", nullptr));
        editExposureSequence->setPlaceholderText(QCoreApplication::translate("SpektrometrClass", "e.g. 1;2;5;10;20", nullptr));
        labelSpectrumPlaceholder->setText(QCoreApplication::translate("SpektrometrClass", "Spectrum chart", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabSpectrum), QCoreApplication::translate("SpektrometrClass", "Spectrum", nullptr));
        btnRefreshResults->setText(QCoreApplication::translate("SpektrometrClass", "Refresh", nullptr));
        btnExportAll->setText(QCoreApplication::translate("SpektrometrClass", "Export all", nullptr));
        btnOpenMeasurement->setText(QCoreApplication::translate("SpektrometrClass", "Open selected", nullptr));
        btnDeleteSelected->setText(QCoreApplication::translate("SpektrometrClass", "Delete selected", nullptr));
        btnDeleteAll->setText(QCoreApplication::translate("SpektrometrClass", "Delete all", nullptr));
        labelResultsInfo->setText(QCoreApplication::translate("SpektrometrClass", "Measurements: 0", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabResults), QCoreApplication::translate("SpektrometrClass", "Results", nullptr));
        labelScanWidth->setText(QCoreApplication::translate("SpektrometrClass", "Scan width (um)", nullptr));
        labelScanHeight->setText(QCoreApplication::translate("SpektrometrClass", "Scan height (um)", nullptr));
        labelStepX->setText(QCoreApplication::translate("SpektrometrClass", "Step X (um)", nullptr));
        labelStepY->setText(QCoreApplication::translate("SpektrometrClass", "Step Y (um)", nullptr));
        labelPortX->setText(QCoreApplication::translate("SpektrometrClass", "Port X", nullptr));
        labelPortY->setText(QCoreApplication::translate("SpektrometrClass", "Port Y", nullptr));
        labelResultsFolderPath->setText(QCoreApplication::translate("SpektrometrClass", "Results folder", nullptr));
        editResultsFolderPath->setText(QCoreApplication::translate("SpektrometrClass", "measurement_data", nullptr));
        editResultsFolderPath->setPlaceholderText(QCoreApplication::translate("SpektrometrClass", "measurement_data", nullptr));
        btnBrowseResultsFolder->setText(QCoreApplication::translate("SpektrometrClass", "Browse", nullptr));
        btnSaveSettings->setText(QCoreApplication::translate("SpektrometrClass", "Save settings", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabSettings), QCoreApplication::translate("SpektrometrClass", "Settings", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SpektrometrClass: public Ui_SpektrometrClass {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SPEKTROMETR_H

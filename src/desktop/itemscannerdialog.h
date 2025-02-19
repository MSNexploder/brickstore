// Copyright (C) 2004-2024 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <functional>

#include <QVector>
#include <QDialog>
#include <QCamera>
#include <QElapsedTimer>
#include <QTimer>
#include <QDeadlineTimer>

#include "bricklink/global.h"
#include "scanner/itemscanner.h"

QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QButtonGroup)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QToolButton)
QT_FORWARD_DECLARE_CLASS(QProgressBar)
QT_FORWARD_DECLARE_CLASS(QCameraDevice)
QT_FORWARD_DECLARE_CLASS(QCameraViewfinder)
QT_FORWARD_DECLARE_CLASS(QMediaCaptureSession)
QT_FORWARD_DECLARE_CLASS(QImageCapture)
QT_FORWARD_DECLARE_CLASS(QStackedLayout)
QT_FORWARD_DECLARE_CLASS(QMediaDevices)

class CameraPreviewWidget;


class ItemScannerDialog : public QDialog
{
    Q_OBJECT
public:
    static void checkSystemPermissions(QObject *context, const std::function<void(bool)> &callback);

    explicit ItemScannerDialog(QWidget *parent = nullptr);
    ~ItemScannerDialog() override;

    void setItemType(const BrickLink::ItemType *itemType);

signals:
    void itemsScanned(const QVector<const BrickLink::Item *> &items);

protected:
    void changeEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void updateCameraDevices();
    void updateCamera(const QByteArray &cameraId);
    void updateCameraResolution(int preferredImageSize);
    void updateBackend(const QByteArray &backendId);
    void setupCapture();
    bool canCapture() const;
    void capture();
    void onScanFinished(uint id, const QVector<ItemScanner::Result> &itemsAndScores);
    void onScanFailed(uint id, const QString &error);
    void languageChange();

    std::unique_ptr<QMediaDevices> m_mediaDevices;
    std::unique_ptr<QCamera> m_camera;
    QComboBox *m_selectCamera;
    QComboBox *m_selectBackend;
    QButtonGroup *m_selectItemType;
    QLabel *m_labelCamera;
    QLabel *m_labelBackend;
    QLabel *m_labelItemType;
    CameraPreviewWidget *m_cameraPreviewWidget;
    QMediaCaptureSession *m_captureSession;
    QImageCapture *m_imageCapture;
    QTimer m_cameraStopTimer;

    QLabel *m_status;
    QProgressBar *m_progress;
    QTimer *m_progressTimer;
    QStackedLayout *m_bottomStack;
    QToolButton *m_pinWindow;

    std::optional<int> m_currentCaptureId;
    QDeadlineTimer m_tryCaptureBefore;
    uint m_currentScan = 0;
    static int s_averageScanTime;
    QElapsedTimer m_lastScanTime;

    QString m_lastError;
    QTimer m_noMatchMessageTimeout;
    QTimer m_errorMessageTimeout;

    enum class State : int {
        Idle,         // -> Capturing / Window inactive -> SoonInactive
        Capturing,    // -> Scanning | Error
        Scanning,     // -> Idle | NoMatch | Error
        NoMatch,      // 10sec -> Idle
        Error,        // 10sec -> Idle
        SoonInactive, // 10sec -> Inactive / Window active -> Idle
        Inactive,     // Click -> Idle [dark overlay, play button]
    };
    State m_state = State::Idle;
    void setState(State newState);

    QVector<const BrickLink::ItemType *> m_validItemTypes;

    static bool s_hasCameraPermission;
};

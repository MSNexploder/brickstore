// Copyright (C) 2004-2024 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtDebug>
#include <QElapsedTimer>


class stopwatch
{
public:
    stopwatch(const char *desc)
    {
        m_label = desc;
        m_timer.start();
    }
    ~stopwatch()
    {
        restart();
    }
    void restart(const char *desc = nullptr)
    {
        qint64 micros = m_timer.nsecsElapsed() / 1000;

        int sec = 0;
        if (micros > 1000 * 1000) {
            sec = int(micros / (1000 * 1000));
            micros %= (1000 * 1000);
        }
        int msec = int(micros / 1000);
        int usec = micros % 1000;

        qInfo("%s: %ds %03dms %03dus", m_label ? m_label : "", sec, msec, usec);
        m_label = desc;
        m_timer.restart();
    }

private:
    Q_DISABLE_COPY(stopwatch)

    const char *m_label;
    QElapsedTimer m_timer;
};

//
// main.cpp
// This file is part of Ren Garden
// Copyright (C) 2015 MetÆducation
//
// Ren Garden is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Ren Garden is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Ren Garden.  If not, see <http://www.gnu.org/licenses/>.
//
// See http://ren-garden.metaeducation.com for more information on this project
//

#include <QApplication>

#include <QThread>
#include <QMessageBox>

#include "mainwindow.h"

#ifndef NDEBUG

#include <iostream>

// http://blog.hostilefork.com/qt-essential-noisy-debug-hook/
void noisyFailureMsgHandler(
    QtMsgType type,
    QMessageLogContext const &,
    QString const & msg
) {
    QByteArray array = msg.toLocal8Bit();

    std::cerr << array.data();
    std::cerr.flush();

    // Why didn't Qt want to make failed signal/slot connections qWarning?!
    // It is true that with C++11 the proper use of the method signature
    // prevents problems, but there may be a mixture of code (or even cases
    // inside of Qt itself?) which still uses the old string-based style.

    if ((type == QtDebugMsg) and msg.contains("::connect"))
        type = QtWarningMsg;


    // this is another one that doesn't make sense as just a debug message.
    // It's a pretty serious sign of a problem.  See link in blog entry.

    if ((type == QtDebugMsg)
            and msg.contains("QPainter::begin")
            and msg.contains("Paint device returned engine")) {
        type = QtWarningMsg;
    }


    // This qWarning about "Cowardly refusing to send clipboard message to
    // hung application..." is something that can easily happen if you are
    // debugging and the application is paused.  As it is so common, not worth
    // popping up a dialog.

    if (
        QString(msg).contains("QClipboard::event")
        and QString(msg).contains("Cowardly refusing")
    ) {
        type = QtDebugMsg;
    }


    // only the GUI thread should display message boxes.  If you are
    // writing a multithreaded application and the error happens on
    // a non-GUI thread, you'll have to queue the message to the GUI

    QCoreApplication * instance = QCoreApplication::instance();
    const bool isGuiThread =
        instance and (QThread::currentThread() == instance->thread());

    if (isGuiThread) {
        QMessageBox box;
        switch (type) {
        case QtDebugMsg:
            return;
        case QtWarningMsg:
            box.setIcon(QMessageBox::Warning);
            box.setInformativeText(msg);
            box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            break;
        case QtCriticalMsg:
            box.setIcon(QMessageBox::Critical);
            box.setInformativeText(msg);
            box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            break;
        case QtFatalMsg:
            box.setIcon(QMessageBox::Critical);
            box.setInformativeText(msg);
            box.setStandardButtons(QMessageBox::Cancel);
            break;
        }

        int ret = box.exec();
        if (ret == QMessageBox::Cancel)
            abort();
    }
    else {
        if (type != QtDebugMsg)
            abort(); // be NOISY unless overridden!
    }
}
#endif


int main(int argc, char *argv[])
{
    // Q_INIT_RESOURCE(ren-garden);

    QApplication app(argc, argv);

#ifndef NDEBUG
        // Because our "noisy" message handler uses the GUI subsystem for
        // message boxes, we can't install it until after the QApplication is
        // constructed.  But it is good to be the very next thing to run, to
        // start catching warnings ASAP.
        QtMessageHandler oldMsgHandler
            = qInstallMessageHandler(noisyFailureMsgHandler);
#endif

    MainWindow mainWin;
    mainWin.show();
    return app.exec();
}

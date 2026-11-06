// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <QString>

#include "packcode/PackServerSetup.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;

class PackServerDialog : public QDialog {
    Q_OBJECT
   public:
    explicit PackServerDialog(QWidget* parent = nullptr);

   private:
    void prepare();
    void startServer();
    void stopServer();
    void appendLog(const QString& line);

    QLineEdit* m_code = nullptr;
    QLineEdit* m_port = nullptr;
    QLineEdit* m_memory = nullptr;
    QCheckBox* m_eula = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPushButton* m_prepare = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_stop = nullptr;
    PackCode::ServerSetup* m_setup = nullptr;
    QProcess* m_process = nullptr;
    QString m_directory;
    QString m_java;
    QStringList m_arguments;
    bool m_ready = false;
};

// SPDX-License-Identifier: GPL-3.0-only
#include "PackServerDialog.h"

#include "Application.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

PackServerDialog::PackServerDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("실험: 서버"));
    resize(720, 560);

    auto* warning = new QLabel(
        tr("실험 기능입니다. 이 컴퓨터에서 마크 서버를 켭니다. 코드와 같은 버전과 모드로 폴더를 만들고, 클라이언트 전용 모드는 빼 둡니다. 포트는 직접 열어야 합니다."),
        this);
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color: #9b1c1c; font-weight: 600;"));

    m_code = new QLineEdit(this);
    m_code->setPlaceholderText(tr("pc1. 코드를 붙여 넣으세요"));
    m_port = new QLineEdit(QStringLiteral("25565"), this);
    m_memory = new QLineEdit(QStringLiteral("2G"), this);
    m_eula = new QCheckBox(tr("Mojang 서버 EULA에 동의합니다. 동의해야 서버를 만들 수 있습니다."), this);
    m_prepare = new QPushButton(tr("폴더 만들기"), this);
    m_start = new QPushButton(tr("시작"), this);
    m_stop = new QPushButton(tr("중지"), this);
    m_start->setEnabled(false);
    m_stop->setEnabled(false);
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);

    auto* form = new QFormLayout();
    form->addRow(tr("코드"), m_code);
    form->addRow(tr("포트"), m_port);
    form->addRow(tr("메모리"), m_memory);

    auto* actions = new QHBoxLayout();
    actions->addWidget(m_prepare);
    actions->addWidget(m_start);
    actions->addWidget(m_stop);
    actions->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(warning);
    layout->addLayout(form);
    layout->addWidget(m_eula);
    layout->addLayout(actions);
    layout->addWidget(m_log, 1);

    connect(m_prepare, &QPushButton::clicked, this, &PackServerDialog::prepare);
    connect(m_start, &QPushButton::clicked, this, &PackServerDialog::startServer);
    connect(m_stop, &QPushButton::clicked, this, &PackServerDialog::stopServer);
    connect(this, &QDialog::finished, this, [this](int) { stopServer(); });
}

void PackServerDialog::prepare()
{
    if (!m_prepare->isEnabled())
        return;
    if (!m_eula->isChecked()) {
        appendLog(tr("EULA에 동의해야 합니다."));
        return;
    }

    PackCode::Preset preset;
    try {
        preset = PackCode::decode(m_code->text().toStdString());
    } catch (const PackCode::Error& error) {
        appendLog(QString::fromUtf8(error.what()));
        return;
    }

    bool portOk = false;
    const int port = m_port->text().trimmed().toInt(&portOk);
    if (!portOk || port < 1 || port > 65535) {
        appendLog(tr("포트는 1에서 65535 사이여야 합니다."));
        return;
    }

    const QString memory = m_memory->text().trimmed();
    static const QRegularExpression memoryPattern(QStringLiteral("^[0-9]+[GgMm]$"));
    if (!memoryPattern.match(memory).hasMatch()) {
        appendLog(tr("메모리는 2G 처럼 적으세요."));
        return;
    }

    const QString directory =
        APPLICATION->dataRoot() + QStringLiteral("/servers/") + QString::fromStdString(preset.instanceDirectoryName());
    QString java = APPLICATION->javaPath().trimmed();
    const QFileInfo javaInfo(java);
    if (javaInfo.isDir()) {
        const QString binary = QDir(java).filePath(
#ifdef Q_OS_WIN
            QStringLiteral("bin/java.exe")
#else
            QStringLiteral("bin/java")
#endif
        );
        if (QFileInfo::exists(binary))
            java = binary;
    }

    if (!m_setup) {
        m_setup = new PackCode::ServerSetup(this);
        connect(m_setup, &PackCode::ServerSetup::status, this, &PackServerDialog::appendLog);
        connect(m_setup, &PackCode::ServerSetup::failed, this, [this](const QString& reason) {
            appendLog(reason);
            m_prepare->setEnabled(true);
        });
        connect(m_setup, &PackCode::ServerSetup::succeeded, this,
                [this](const QString& readyDirectory, const QString& javaExecutable, const QStringList& arguments) {
                    m_directory = readyDirectory;
                    m_java = javaExecutable;
                    m_arguments = arguments;
                    m_ready = true;
                    m_start->setEnabled(!m_process || m_process->state() == QProcess::NotRunning);
                    m_prepare->setEnabled(true);
                    appendLog(readyDirectory);
                });
    }

    m_ready = false;
    m_start->setEnabled(false);
    m_prepare->setEnabled(false);
    m_setup->start(preset, directory, port, memory, java);
}

void PackServerDialog::startServer()
{
    if (!m_ready || (m_process && m_process->state() != QProcess::NotRunning))
        return;

    auto* process = new QProcess(this);
    m_process = process;
    process->setWorkingDirectory(m_directory);
    process->setProgram(m_java);
    process->setArguments(m_arguments);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyRead, this, [this, process] { appendLog(QString::fromLocal8Bit(process->readAll())); });
    connect(process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        appendLog(tr("서버가 끝났습니다."));
        m_start->setEnabled(m_ready);
        m_stop->setEnabled(false);
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        appendLog(tr("자바를 실행하지 못했습니다. 설정의 자바 경로를 확인하세요."));
        m_start->setEnabled(m_ready);
        m_stop->setEnabled(false);
    });

    m_start->setEnabled(false);
    m_stop->setEnabled(true);
    process->start();
}

void PackServerDialog::stopServer()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;

    QProcess* process = m_process;
    process->write("stop\n");
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process, timer] {
        timer->deleteLater();
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    timer->start(8000);
}

void PackServerDialog::appendLog(const QString& line)
{
    QString text = line;
    if (!text.endsWith(QChar('\n')))
        text += QChar('\n');
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->moveCursor(QTextCursor::End);
}

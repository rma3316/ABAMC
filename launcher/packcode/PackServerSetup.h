// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "packcode/PackPreset.h"

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace PackCode {

class ServerSetup : public QObject {
    Q_OBJECT
   public:
    explicit ServerSetup(QObject* parent = nullptr);

    // directory is created. javaExecutable may be empty; then "java" is used.
    // Caller must already have confirmed the Minecraft EULA.
    void start(const Preset& preset, const QString& directory, int port, const QString& memory, const QString& javaExecutable);

   signals:
    void status(const QString& line);
    void succeeded(const QString& directory, const QString& javaExecutable, const QStringList& arguments);
    void failed(const QString& reason);

   private:
    void fail(const QString& reason);
    void writeBasics();
    void fetchNextMod();
    void onModMetadata();
    void downloadModJar(const QString& url, const QString& fileName);
    void onModJar();
    void downloadLoader();
    void onLoaderJar();
    void runNeoForgeInstaller();
    void finishPlan();
    bool includeEnvironment(const QString& environment, QString* note) const;

    Preset m_preset;
    QString m_directory;
    QString m_java;
    QString m_memory;
    int m_port = 25565;
    int m_modIndex = 0;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    QProcess* m_installer = nullptr;
    QString m_pendingJarName;
    bool m_pendingInclude = false;
    QStringList m_arguments;
};

}  // namespace PackCode

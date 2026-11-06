// SPDX-License-Identifier: GPL-3.0-only
#include "PackServerSetup.h"

#include "BuildConfig.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QUrl>

namespace PackCode {
namespace {
QString memoryFlag(QString memory)
{
    memory = memory.trimmed();
    if (memory.endsWith(QLatin1Char('g')))
        memory.chop(1), memory += QLatin1Char('G');
    if (memory.endsWith(QLatin1Char('m')))
        memory.chop(1), memory += QLatin1Char('M');
    return memory;
}

QNetworkRequest requestFor(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ABAMC/1.0.2 (server experiment)"));
    return request;
}
}  // namespace

ServerSetup::ServerSetup(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

void ServerSetup::start(const Preset& preset, const QString& directory, int port, const QString& memory, const QString& javaExecutable)
{
    m_preset = preset;
    m_directory = directory;
    m_port = port;
    m_memory = memoryFlag(memory);
    m_java = javaExecutable.trimmed().isEmpty() ? QStringLiteral("java") : javaExecutable.trimmed();
    m_modIndex = 0;
    if (!QDir().mkpath(m_directory)) {
        fail(QStringLiteral("서버 폴더를 만들지 못했습니다."));
        return;
    }
    writeBasics();
    fetchNextMod();
}

void ServerSetup::fail(const QString& reason)
{
    if (m_port < 0)
        return;
    m_port = -1;
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    emit failed(reason);
}

void ServerSetup::writeBasics()
{
    QFile eula(QDir(m_directory).filePath(QStringLiteral("eula.txt")));
    if (!eula.open(QIODevice::WriteOnly) || eula.write("eula=true\n") < 0) {
        fail(QStringLiteral("EULA 파일을 쓰지 못했습니다."));
        return;
    }
    QFile properties(QDir(m_directory).filePath(QStringLiteral("server.properties")));
    const QByteArray body = QStringLiteral("server-port=%1\nonline-mode=true\nmotd=ABAMC\nenable-query=false\n").arg(m_port).toUtf8();
    if (!properties.open(QIODevice::WriteOnly) || properties.write(body) != body.size()) {
        fail(QStringLiteral("서버 설정을 쓰지 못했습니다."));
        return;
    }
    emit status(QStringLiteral("서버 폴더를 만들었습니다."));
}

bool ServerSetup::includeEnvironment(const QString& environment, QString* note) const
{
    if (environment == QLatin1String("client_only") || environment == QLatin1String("singleplayer_only")) {
        *note = QStringLiteral("클라이언트 전용이라 빼둡니다:");
        return false;
    }
    if (environment.isEmpty() || environment == QLatin1String("unknown")) {
        *note = QStringLiteral("서버/클라이언트 구분이 없어 넣습니다:");
        return true;
    }
    *note = QStringLiteral("서버에 넣습니다:");
    return true;
}

void ServerSetup::fetchNextMod()
{
    if (m_port < 0)
        return;
    if (m_modIndex >= static_cast<int>(m_preset.mods.size())) {
        downloadLoader();
        return;
    }
    const ModRef& mod = m_preset.mods[static_cast<size_t>(m_modIndex)];
    const QString project = QString::fromStdString(mod.projectId);
    emit status(QStringLiteral("모드 확인: %1").arg(project));
    if (!mod.versionId) {
        emit status(QStringLiteral("버전 번호가 없어 빼둡니다: %1").arg(project));
        ++m_modIndex;
        fetchNextMod();
        return;
    }
    const QString version = QString::fromStdString(*mod.versionId);
    const bool curseForge = mod.source == ModSource::CurseForge;
    const QUrl url = curseForge ? QUrl(BuildConfig.FLAME_BASE_URL + QStringLiteral("/mods/%1/files/%2").arg(project, version))
                                : QUrl(BuildConfig.MODRINTH_PROD_URL + QStringLiteral("/version/") + version);
    m_reply = m_network->get(requestFor(url));
    connect(m_reply, &QNetworkReply::finished, this, [this, curseForge, project] {
        if (m_port < 0)
            return;
        const QByteArray body = m_reply->readAll();
        const bool failed = m_reply->error() != QNetworkReply::NoError;
        m_reply->deleteLater();
        m_reply = nullptr;
        if (failed) {
            fail(QStringLiteral("모드 정보를 받지 못했습니다: %1").arg(project));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        if (curseForge) {
            emit status(QStringLiteral("CurseForge 모드는 서버/클라이언트 구분이 없어 넣습니다: %1").arg(project));
            const QJsonObject data = root.value(QStringLiteral("data")).toObject();
            const QString fileName = QFileInfo(data.value(QStringLiteral("fileName")).toString()).fileName();
            const QString downloadUrl = data.value(QStringLiteral("downloadUrl")).toString();
            if (!downloadUrl.isEmpty()) {
                downloadModJar(downloadUrl, fileName);
                return;
            }
            const QString version = QString::fromStdString(*m_preset.mods[static_cast<size_t>(m_modIndex)].versionId);
            m_reply = m_network->get(requestFor(QUrl(BuildConfig.FLAME_BASE_URL + QStringLiteral("/mods/%1/files/%2/download-url").arg(project, version))));
            connect(m_reply, &QNetworkReply::finished, this, [this, project, fileName] {
                if (m_port < 0)
                    return;
                const QByteArray nested = m_reply->readAll();
                m_reply->deleteLater();
                m_reply = nullptr;
                const QString downloadUrl = QJsonDocument::fromJson(nested).object().value(QStringLiteral("data")).toString();
                if (downloadUrl.isEmpty()) {
                    fail(QStringLiteral("모드 주소를 받지 못했습니다: %1").arg(project));
                    return;
                }
                downloadModJar(downloadUrl, fileName);
            });
            return;
        }
        QString note;
        const bool include = includeEnvironment(root.value(QStringLiteral("environment")).toString(), &note);
        emit status(note + QStringLiteral(" ") + project);
        if (!include) {
            ++m_modIndex;
            fetchNextMod();
            return;
        }
        const QJsonArray files = root.value(QStringLiteral("files")).toArray();
        QString jarUrl;
        QString fileName;
        for (const QJsonValue& value : files) {
            const QJsonObject file = value.toObject();
            if (jarUrl.isEmpty() || file.value(QStringLiteral("primary")).toBool()) {
                jarUrl = file.value(QStringLiteral("url")).toString();
                fileName = QFileInfo(file.value(QStringLiteral("filename")).toString()).fileName();
                if (file.value(QStringLiteral("primary")).toBool() && !jarUrl.isEmpty())
                    break;
            }
        }
        if (jarUrl.isEmpty() || !fileName.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive)) {
            fail(QStringLiteral("모드 파일을 찾지 못했습니다: %1").arg(project));
            return;
        }
        downloadModJar(jarUrl, fileName);
    });
}

void ServerSetup::onModMetadata() {}

void ServerSetup::downloadModJar(const QString& url, const QString& fileName)
{
    if (m_port < 0)
        return;
    if (!QDir().mkpath(QDir(m_directory).filePath(QStringLiteral("mods")))) {
        fail(QStringLiteral("모드 폴더를 만들지 못했습니다."));
        return;
    }
    m_pendingJarName = fileName;
    m_reply = m_network->get(requestFor(QUrl(url)));
    connect(m_reply, &QNetworkReply::finished, this, &ServerSetup::onModJar);
}

void ServerSetup::onModJar()
{
    if (m_port < 0)
        return;
    const QByteArray body = m_reply->readAll();
    const bool failed = m_reply->error() != QNetworkReply::NoError;
    m_reply->deleteLater();
    m_reply = nullptr;
    if (failed || body.isEmpty()) {
        fail(QStringLiteral("모드를 받지 못했습니다: %1").arg(m_pendingJarName));
        return;
    }
    QSaveFile output(QDir(m_directory).filePath(QStringLiteral("mods/") + m_pendingJarName));
    if (!output.open(QIODevice::WriteOnly) || output.write(body) != body.size() || !output.commit()) {
        fail(QStringLiteral("모드를 쓰지 못했습니다: %1").arg(m_pendingJarName));
        return;
    }
    ++m_modIndex;
    fetchNextMod();
}

void ServerSetup::downloadLoader()
{
    if (m_port < 0)
        return;
    if (!m_preset.loaderVersion) {
        fail(QStringLiteral("로더 버전이 없습니다."));
        return;
    }
    const QString game = QString::fromStdString(m_preset.minecraftVersion);
    const QString loader = QString::fromStdString(*m_preset.loaderVersion);
    QUrl url;
    if (m_preset.loader == ModLoader::Fabric) {
        url = QUrl(QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1/%2/server/jar").arg(game, loader));
        m_pendingJarName = QStringLiteral("fabric-server-launch.jar");
    } else {
        url = QUrl(QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(loader));
        m_pendingJarName = QStringLiteral("neoforge-installer.jar");
    }
    emit status(QStringLiteral("로더를 받는 중"));
    m_reply = m_network->get(requestFor(url));
    connect(m_reply, &QNetworkReply::finished, this, &ServerSetup::onLoaderJar);
}

void ServerSetup::onLoaderJar()
{
    if (m_port < 0)
        return;
    const QByteArray body = m_reply->readAll();
    const bool failed = m_reply->error() != QNetworkReply::NoError;
    m_reply->deleteLater();
    m_reply = nullptr;
    if (failed || body.size() < 1000) {
        fail(QStringLiteral("로더를 받지 못했습니다."));
        return;
    }
    QSaveFile output(QDir(m_directory).filePath(m_pendingJarName));
    if (!output.open(QIODevice::WriteOnly) || output.write(body) != body.size() || !output.commit()) {
        fail(QStringLiteral("로더를 쓰지 못했습니다."));
        return;
    }
    if (m_preset.loader == ModLoader::NeoForge) {
        runNeoForgeInstaller();
        return;
    }
    m_arguments = {QStringLiteral("-Xms1G"), QStringLiteral("-Xmx") + m_memory, QStringLiteral("-jar"), QStringLiteral("fabric-server-launch.jar"),
                  QStringLiteral("nogui")};
    finishPlan();
}

void ServerSetup::runNeoForgeInstaller()
{
    emit status(QStringLiteral("NeoForge 서버를 설치하는 중"));
    m_installer = new QProcess(this);
    m_installer->setWorkingDirectory(m_directory);
    m_installer->setProgram(m_java);
    m_installer->setArguments({QStringLiteral("-jar"), QStringLiteral("neoforge-installer.jar"), QStringLiteral("--installServer")});
    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_installer, &QProcess::readyRead, this, [this] { emit status(QString::fromLocal8Bit(m_installer->readAll()).trimmed()); });
    connect(m_installer, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_port < 0)
            return;
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            fail(QStringLiteral("NeoForge 설치가 실패했습니다. 종료 코드 %1").arg(exitCode));
            return;
        }
        QString relative;
        QDirIterator iterator(m_directory, {QStringLiteral("unix_args.txt")}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (path.contains(QStringLiteral("neoforge"))) {
                relative = QDir(m_directory).relativeFilePath(path);
                break;
            }
        }
        if (relative.isEmpty()) {
            fail(QStringLiteral("NeoForge 서버 실행 파일을 찾지 못했습니다."));
            return;
        }
        QFile jvmArgs(QDir(m_directory).filePath(QStringLiteral("user_jvm_args.txt")));
        const QByteArray args = QStringLiteral("-Xms1G\n-Xmx%1\n").arg(m_memory).toUtf8();
        if (!jvmArgs.open(QIODevice::WriteOnly) || jvmArgs.write(args) != args.size()) {
            fail(QStringLiteral("자바 인자 파일을 쓰지 못했습니다."));
            return;
        }
        m_arguments = {QStringLiteral("@user_jvm_args.txt"), QStringLiteral("@") + relative, QStringLiteral("nogui")};
        finishPlan();
    });
    m_installer->start();
    if (m_installer->state() == QProcess::NotRunning && m_installer->error() == QProcess::FailedToStart)
        fail(QStringLiteral("자바를 실행하지 못했습니다."));
}

void ServerSetup::finishPlan()
{
    if (m_port < 0)
        return;
    emit status(QStringLiteral("서버 폴더가 준비되었습니다."));
    emit succeeded(m_directory, m_java, m_arguments);
}

}  // namespace PackCode

// SPDX-License-Identifier: GPL-3.0-only
#include "PackCodeCreationTask.h"

#include "BuildConfig.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/ApiRequest.h"
#include "settings/INISettingsObject.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

PackCodeCreationTask::PackCodeCreationTask(PackCode::Preset preset) : m_preset(std::move(preset)) {}

void PackCodeCreationTask::executeTask()
{
    try {
        m_preset = m_preset.normalized();
    } catch (const PackCode::Error& error) {
        emitFailed(QString::fromUtf8(error.what()));
        return;
    }
    if (!m_preset.loaderVersion) {
        emitFailed(tr("로더 버전이 없습니다."));
        return;
    }
    if (m_preset.loader == PackCode::ModLoader::NeoForge && !PackCode::minecraftAtLeast121(m_preset.minecraftVersion)) {
        emitFailed(tr("NeoForge는 마크 1.21 이상입니다."));
        return;
    }
    for (const auto& mod : m_preset.mods) {
        if (!mod.versionId) {
            emitFailed(tr("모드 버전이 고정되어 있지 않습니다: %1").arg(QString::fromStdString(mod.projectId)));
            return;
        }
    }
    if (name().isEmpty())
        setName(QString::fromStdString(m_preset.name));

    setStatus(tr("%1 인스턴스를 만드는 중").arg(QString::fromStdString(m_preset.name)));
    m_instance = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);
    {
        const SettingsObject::Lock lock(m_instance->settings());
        auto* components = m_instance->getPackProfile();
        components->buildingFromScratch();
        components->setComponentVersion("net.minecraft", QString::fromStdString(m_preset.minecraftVersion), true);
        const QString loaderUid =
            m_preset.loader == PackCode::ModLoader::Fabric ? QStringLiteral("net.fabricmc.fabric-loader") : QStringLiteral("net.neoforged");
        components->setComponentVersion(loaderUid, QString::fromStdString(*m_preset.loaderVersion), true);
        m_instance->setName(name());
        m_instance->setIconKey(m_instIcon);
        components->saveNow();
    }

    try {
        writeServerList();
    } catch (const PackCode::Error& error) {
        emitFailed(QString::fromUtf8(error.what()));
        return;
    }

    m_modsDir = FS::PathCombine(m_stagingPath, "minecraft", "mods");
    m_modIndex = 0;
    fetchNextMod();
}

bool PackCodeCreationTask::abort()
{
    if (!canAbort())
        return false;
    if (m_gameFilesTask)
        return InstanceTask::abort();
    if (m_request && m_request->isRunning())
        return m_request->abort();
    return InstanceTask::abort();
}

void PackCodeCreationTask::writeServerList()
{
    if (!m_preset.server)
        return;
    const QString path = FS::PathCombine(m_stagingPath, "minecraft", "servers.dat");
    QDir().mkpath(QFileInfo(path).absolutePath());
    const auto bytes = PackCode::writeServersDat({*m_preset.server});
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw PackCode::Error("서버 목록을 쓰지 못했습니다.");
    if (file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()))
        throw PackCode::Error("서버 목록을 쓰지 못했습니다.");
}

void PackCodeCreationTask::fetchNextMod()
{
    if (m_modIndex >= static_cast<int>(m_preset.mods.size())) {
        finishInstance();
        return;
    }
    const auto& mod = m_preset.mods[static_cast<size_t>(m_modIndex)];
    setStatus(tr("모드를 받는 중: %1").arg(QString::fromStdString(mod.projectId)));
    setProgress(m_modIndex, static_cast<qint64>(m_preset.mods.size()));
    setAbortable(true);
    const bool curseForge = mod.source == PackCode::ModSource::CurseForge;
    const QUrl url = curseForge ? QUrl(BuildConfig.FLAME_BASE_URL + "/mods/" + QString::fromStdString(mod.projectId) + "/files/" +
                                       QString::fromStdString(*mod.versionId))
                                : QUrl(BuildConfig.MODRINTH_PROD_URL + "/version/" + QString::fromStdString(*mod.versionId));
    auto [request, response] = Net::ApiRequest::makeByteArray(url);
    m_request = request;
    m_response = response;
    connect(request.get(), &Task::succeeded, this, curseForge ? &PackCodeCreationTask::onCurseForgeFile : &PackCodeCreationTask::onVersionMetadata);
    connect(request.get(), &Task::failed, this, [this](QString reason) { emitFailed(reason); });
    request->start();
}

void PackCodeCreationTask::onCurseForgeFile()
{
    const QJsonObject data = QJsonDocument::fromJson(m_response ? *m_response : QByteArray()).object().value("data").toObject();
    m_curseFileName = QFileInfo(data.value("fileName").toString()).fileName();
    const QString downloadUrl = data.value("downloadUrl").toString();
    if (m_curseFileName.isEmpty() || m_curseFileName == "." || m_curseFileName == "..") {
        emitFailed(tr("모드 파일을 찾지 못했습니다."));
        return;
    }
    if (!downloadUrl.isEmpty()) {
        downloadModFile(downloadUrl, m_curseFileName);
        return;
    }
    const auto& mod = m_preset.mods[static_cast<size_t>(m_modIndex)];
    const QUrl url(BuildConfig.FLAME_BASE_URL + "/mods/" + QString::fromStdString(mod.projectId) + "/files/" +
                   QString::fromStdString(*mod.versionId) + "/download-url");
    auto [request, response] = Net::ApiRequest::makeByteArray(url);
    m_request = request;
    m_response = response;
    connect(request.get(), &Task::succeeded, this, &PackCodeCreationTask::onCurseForgeDownloadUrl);
    connect(request.get(), &Task::failed, this, [this](QString reason) { emitFailed(reason); });
    request->start();
}

void PackCodeCreationTask::onCurseForgeDownloadUrl()
{
    const QString downloadUrl = QJsonDocument::fromJson(m_response ? *m_response : QByteArray()).object().value("data").toString();
    if (downloadUrl.isEmpty()) {
        emitFailed(tr("모드 파일을 찾지 못했습니다."));
        return;
    }
    downloadModFile(downloadUrl, m_curseFileName);
}

void PackCodeCreationTask::onVersionMetadata()
{
    const QJsonDocument document = QJsonDocument::fromJson(m_response ? *m_response : QByteArray());
    const QJsonArray files = document.object().value("files").toArray();
    QString url;
    QString fileName;
    for (const QJsonValue& value : files) {
        const QJsonObject file = value.toObject();
        if (url.isEmpty() || file.value("primary").toBool()) {
            url = file.value("url").toString();
            fileName = QFileInfo(file.value("filename").toString()).fileName();
            if (file.value("primary").toBool() && !url.isEmpty() && !fileName.isEmpty())
                break;
        }
    }
    if (url.isEmpty() || fileName.isEmpty() || fileName == "." || fileName == "..") {
        emitFailed(tr("모드 파일을 찾지 못했습니다."));
        return;
    }
    downloadModFile(url, fileName);
}

void PackCodeCreationTask::downloadModFile(const QString& url, const QString& fileName)
{
    QDir().mkpath(m_modsDir);
    auto request = Net::ApiRequest::makeFile(QUrl(url), FS::PathCombine(m_modsDir, fileName));
    m_request = request;
    m_response = nullptr;
    connect(request.get(), &Task::succeeded, this, &PackCodeCreationTask::onModFile);
    connect(request.get(), &Task::failed, this, [this](QString reason) { emitFailed(reason); });
    request->start();
}

void PackCodeCreationTask::onModFile()
{
    ++m_modIndex;
    fetchNextMod();
}

void PackCodeCreationTask::finishInstance()
{
    setProgress(m_preset.mods.size(), std::max<qint64>(1, static_cast<qint64>(m_preset.mods.size())));
    downloadFiles(m_instance.get());
}

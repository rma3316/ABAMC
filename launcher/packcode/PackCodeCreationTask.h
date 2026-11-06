// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <memory>

#include "InstanceTask.h"
#include "net/Request.h"
#include "packcode/PackPreset.h"

#include <QByteArray>

class MinecraftInstance;

class PackCodeCreationTask final : public InstanceTask {
    Q_OBJECT
   public:
    explicit PackCodeCreationTask(PackCode::Preset preset);

    void executeTask() override;
    bool abort() override;

   private:
    void writeServerList();
    void fetchNextMod();
    void downloadModFile(const QString& url, const QString& fileName);
    void finishInstance();

   private slots:
    void onVersionMetadata();
    void onModFile();

   private:
    PackCode::Preset m_preset;
    std::unique_ptr<MinecraftInstance> m_instance;
    int m_modIndex = 0;
    Net::Request::Ptr m_request;
    QByteArray* m_response = nullptr;
    QString m_modsDir;
};

// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <QString>

#include "net/Request.h"
#include "packcode/PackPreset.h"
#include "tasks/Task.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QByteArray;

class PackCodeCreateDialog : public QDialog {
    Q_OBJECT
   public:
    explicit PackCodeCreateDialog(QWidget* parent = nullptr);

   private:
    struct PinnedMod {
        QString projectId;
        QString versionId;
        QString versionNumber;
        QString title;
    };

    void loadMinecraftVersions();
    void suggestLoaderVersion();
    void searchMods();
    void pinSelected();
    void refreshCode();
    QString loaderId() const;
    QString minecraftVersion() const;

    QLineEdit* m_name = nullptr;
    QComboBox* m_minecraft = nullptr;
    QComboBox* m_loader = nullptr;
    QComboBox* m_loaderVersion = nullptr;
    QLineEdit* m_server = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_results = nullptr;
    QListWidget* m_pinned = nullptr;
    QPlainTextEdit* m_code = nullptr;
    QLabel* m_reason = nullptr;
    Net::Request::Ptr m_request;
    QByteArray* m_response = nullptr;
    int m_requestGeneration = 0;
    Task::Ptr m_metaTask;
    QString m_pendingProject;
    QString m_pendingTitle;
};

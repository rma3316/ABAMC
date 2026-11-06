// SPDX-License-Identifier: GPL-3.0-only
#include "PackCodeCreateDialog.h"

#include "Application.h"
#include "BuildConfig.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "net/ApiRequest.h"

#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {
const QRegularExpression kReleaseVersion(QStringLiteral(R"(^\d+\.\d+(\.\d+)?$)"));
}

PackCodeCreateDialog::PackCodeCreateDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("코드 만들기"));
    resize(760, 560);

    m_name = new QLineEdit(this);
    m_minecraft = new QComboBox(this);
    m_minecraft->setEditable(true);
    m_minecraft->addItems({QStringLiteral("1.21.11"), QStringLiteral("1.21.1"), QStringLiteral("1.21"), QStringLiteral("1.20.1")});
    m_loader = new QComboBox(this);
    m_loader->addItem(QStringLiteral("Fabric"), QStringLiteral("fabric"));
    m_loader->addItem(QStringLiteral("NeoForge"), QStringLiteral("neoforge"));
    m_loaderVersion = new QComboBox(this);
    m_loaderVersion->setEditable(true);
    m_server = new QLineEdit(this);
    m_server->setPlaceholderText(QStringLiteral("play.example.com"));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("모드 이름"));
    m_results = new QListWidget(this);
    m_pinned = new QListWidget(this);
    m_code = new QPlainTextEdit(this);
    m_code->setReadOnly(true);
    m_code->setPlaceholderText(tr("코드 만들기를 누르면 pc1. 코드가 나옵니다."));
    m_reason = new QLabel(this);
    m_reason->setWordWrap(true);

    auto* form = new QFormLayout();
    form->addRow(tr("이름"), m_name);
    form->addRow(tr("마크"), m_minecraft);
    form->addRow(tr("로더"), m_loader);
    form->addRow(tr("로더 버전"), m_loaderVersion);
    form->addRow(tr("서버"), m_server);

    auto* searchButton = new QPushButton(tr("검색"), this);
    auto* searchRow = new QHBoxLayout();
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(searchButton);
    auto* addButton = new QPushButton(tr("추가"), this);
    auto* removeButton = new QPushButton(tr("빼기"), this);
    auto* lists = new QSplitter(this);
    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(tr("결과"), left));
    leftLayout->addWidget(m_results);
    leftLayout->addWidget(addButton);
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->addWidget(new QLabel(tr("담긴 모드"), right));
    rightLayout->addWidget(m_pinned);
    rightLayout->addWidget(removeButton);
    lists->addWidget(left);
    lists->addWidget(right);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* makeButton = buttons->addButton(tr("코드 만들기"), QDialogButtonBox::ActionRole);
    auto* copyButton = buttons->addButton(tr("복사"), QDialogButtonBox::ActionRole);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(searchRow);
    layout->addWidget(lists, 1);
    layout->addWidget(m_code);
    layout->addWidget(m_reason);
    layout->addWidget(buttons);

    connect(searchButton, &QPushButton::clicked, this, &PackCodeCreateDialog::searchMods);
    connect(m_search, &QLineEdit::returnPressed, this, &PackCodeCreateDialog::searchMods);
    connect(addButton, &QPushButton::clicked, this, &PackCodeCreateDialog::pinSelected);
    connect(m_results, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { pinSelected(); });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        delete m_pinned->takeItem(m_pinned->currentRow());
        refreshCode();
    });
    connect(makeButton, &QPushButton::clicked, this, &PackCodeCreateDialog::refreshCode);
    connect(copyButton, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_code->toPlainText());
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_loader, &QComboBox::currentIndexChanged, this, &PackCodeCreateDialog::suggestLoaderVersion);
    connect(m_minecraft, &QComboBox::currentTextChanged, this, &PackCodeCreateDialog::suggestLoaderVersion);

    loadMinecraftVersions();
    suggestLoaderVersion();
}

QString PackCodeCreateDialog::loaderId() const
{
    return m_loader->currentData().toString();
}

QString PackCodeCreateDialog::minecraftVersion() const
{
    return m_minecraft->currentText().trimmed();
}

void PackCodeCreateDialog::loadMinecraftVersions()
{
    auto* index = APPLICATION->metadataIndex();
    auto list = index->get(QStringLiteral("net.minecraft"));
    if (!list)
        return;
    auto fill = [this, list] {
        const QString current = m_minecraft->currentText();
        m_minecraft->clear();
        for (int i = 0; i < list->count(); ++i) {
            const QString version = list->at(i)->descriptor();
            if (kReleaseVersion.match(version).hasMatch())
                m_minecraft->addItem(version);
        }
        if (!current.isEmpty())
            m_minecraft->setCurrentText(current);
    };
    if (list->isLoaded()) {
        fill();
        return;
    }
    m_metaTask = list->getLoadTask();
    if (!m_metaTask)
        return;
    connect(m_metaTask.get(), &Task::succeeded, this, fill);
    m_metaTask->start();
}

void PackCodeCreateDialog::suggestLoaderVersion()
{
    const QString uid = loaderId() == QStringLiteral("neoforge") ? QStringLiteral("net.neoforged") : QStringLiteral("net.fabricmc.fabric-loader");
    auto list = APPLICATION->metadataIndex()->get(uid);
    if (!list)
        return;
    auto fill = [this, list] {
        const QString current = m_loaderVersion->currentText();
        m_loaderVersion->clear();
        for (int i = 0; i < list->count(); ++i)
            m_loaderVersion->addItem(list->at(i)->descriptor());
        if (auto best = list->getLatestForParent(QStringLiteral("net.minecraft"), minecraftVersion()))
            m_loaderVersion->setCurrentText(best->descriptor());
        else if (!current.isEmpty())
            m_loaderVersion->setCurrentText(current);
    };
    if (list->isLoaded()) {
        fill();
        return;
    }
    auto task = list->getLoadTask();
    if (!task)
        return;
    m_metaTask = task;
    connect(task.get(), &Task::succeeded, this, fill);
    task->start();
}

void PackCodeCreateDialog::searchMods()
{
    const QString queryText = m_search->text().trimmed();
    if (queryText.isEmpty() || minecraftVersion().isEmpty())
        return;
    QUrl url(BuildConfig.MODRINTH_PROD_URL + QStringLiteral("/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("query"), queryText);
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("20"));
    const QString facets = QStringLiteral("[[\"project_type:mod\"],[\"versions:%1\"],[\"categories:%2\"]]").arg(minecraftVersion(), loaderId());
    query.addQueryItem(QStringLiteral("facets"), facets);
    url.setQuery(query);
    auto [request, response] = Net::ApiRequest::makeByteArray(url);
    m_request = request;
    m_response = response;
    const int generation = ++m_requestGeneration;
    connect(request.get(), &Task::succeeded, this, [this, generation] {
        if (generation != m_requestGeneration)
            return;
        m_results->clear();
        const QJsonArray hits = QJsonDocument::fromJson(m_response ? *m_response : QByteArray()).object().value("hits").toArray();
        for (const QJsonValue& value : hits) {
            const QJsonObject hit = value.toObject();
            auto* item = new QListWidgetItem(hit.value("title").toString());
            item->setData(Qt::UserRole, hit.value("project_id").toString());
            item->setToolTip(hit.value("description").toString());
            m_results->addItem(item);
        }
        if (m_results->count() == 0)
            m_reason->setText(tr("이 버전과 로더에 맞는 모드가 없습니다."));
        else
            m_reason->clear();
    });
    connect(request.get(), &Task::failed, this, [this](QString reason) { m_reason->setText(reason); });
    request->start();
}

void PackCodeCreateDialog::pinSelected()
{
    auto* item = m_results->currentItem();
    if (!item)
        return;
    m_pendingProject = item->data(Qt::UserRole).toString();
    m_pendingTitle = item->text();
    if (m_pendingProject.isEmpty())
        return;
    QUrl url(BuildConfig.MODRINTH_PROD_URL + "/project/" + m_pendingProject + "/version");
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("game_versions"), QStringLiteral("[\"%1\"]").arg(minecraftVersion()));
    query.addQueryItem(QStringLiteral("loaders"), QStringLiteral("[\"%1\"]").arg(loaderId()));
    url.setQuery(query);
    auto [request, response] = Net::ApiRequest::makeByteArray(url);
    m_request = request;
    m_response = response;
    const int generation = ++m_requestGeneration;
    connect(request.get(), &Task::succeeded, this, [this, generation] {
        if (generation != m_requestGeneration)
            return;
        const QJsonArray versions = QJsonDocument::fromJson(m_response ? *m_response : QByteArray()).array();
        if (versions.isEmpty()) {
            m_reason->setText(tr("이 버전과 로더에 맞는 파일이 없습니다."));
            return;
        }
        const QJsonObject version = versions.first().toObject();
        const QString versionId = version.value("id").toString();
        const QString number = version.value("version_number").toString();
        if (versionId.isEmpty()) {
            m_reason->setText(tr("모드 버전 번호를 받지 못했습니다."));
            return;
        }
        for (int i = 0; i < m_pinned->count(); ++i) {
            if (m_pinned->item(i)->data(Qt::UserRole).toString().compare(m_pendingProject, Qt::CaseInsensitive) == 0) {
                m_reason->setText(tr("모드가 중복되었습니다: %1").arg(m_pendingProject));
                return;
            }
        }
        auto* pinned = new QListWidgetItem(m_pendingTitle + "  " + number);
        pinned->setData(Qt::UserRole, m_pendingProject);
        pinned->setData(Qt::UserRole + 1, versionId);
        pinned->setData(Qt::UserRole + 2, number);
        m_pinned->addItem(pinned);
        m_reason->clear();
        refreshCode();
    });
    connect(request.get(), &Task::failed, this, [this](QString reason) { m_reason->setText(reason); });
    request->start();
}

void PackCodeCreateDialog::refreshCode()
{
    std::vector<PackCode::ModRef> mods;
    for (int i = 0; i < m_pinned->count(); ++i) {
        auto* item = m_pinned->item(i);
        mods.push_back(PackCode::ModRef{item->data(Qt::UserRole).toString().toStdString(), item->data(Qt::UserRole + 1).toString().toStdString()});
    }
    const QString serverText = m_server->text().trimmed();
    std::optional<PackCode::ServerRef> server;
    if (!serverText.isEmpty())
        server = PackCode::ServerRef{serverText.toStdString(), m_name->text().trimmed().toStdString()};
    const QString loaderVersion = m_loaderVersion->currentText().trimmed();
    try {
        if (loaderId() == QStringLiteral("neoforge") && !PackCode::minecraftAtLeast121(minecraftVersion().toStdString()))
            throw PackCode::Error("NeoForge는 마크 1.21 이상입니다.");
        PackCode::Preset preset{m_name->text().toStdString(),
                                minecraftVersion().toStdString(),
                                loaderId() == QStringLiteral("neoforge") ? PackCode::ModLoader::NeoForge : PackCode::ModLoader::Fabric,
                                mods,
                                server,
                                loaderVersion.isEmpty() ? std::nullopt : std::optional<std::string>(loaderVersion.toStdString())};
        if (!preset.loaderVersion)
            throw PackCode::Error("로더 버전이 없습니다.");
        const std::string code = PackCode::encode(preset);
        m_code->setPlainText(QString::fromStdString(code));
        m_reason->clear();
    } catch (const PackCode::Error& error) {
        m_code->clear();
        m_reason->setText(QString::fromUtf8(error.what()));
    }
}

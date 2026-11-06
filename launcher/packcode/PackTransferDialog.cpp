// SPDX-License-Identifier: GPL-3.0-only
#include "PackTransferDialog.h"

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {
const QString kRelayBase = QStringLiteral("http://144.24.85.201:8791");

QString sha256Of(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
}  // namespace

PackTransferDialog::PackTransferDialog(QWidget* parent) : QDialog(parent), m_network(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("실험 기능"));
    resize(640, 520);

    auto* warning = new QLabel(
        tr("위험 기능입니다. 받은 jar는 게임이 실행합니다. 아는 사람이 보낸 코드만 확인하고, 파일 이름과 크기를 본 뒤에 수락하세요. 전송은 암호화되지 않습니다."),
        this);
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color: #9b1c1c; font-weight: 600;"));

    m_sendList = new QListWidget(this);
    m_sendCode = new QLineEdit(this);
    m_sendCode->setReadOnly(true);
    m_sendStatus = new QLabel(this);
    m_sendStatus->setWordWrap(true);
    auto* choose = new QPushButton(tr("jar 고르기"), this);
    auto* make = new QPushButton(tr("코드 만들기"), this);
    auto* sendPage = new QWidget(this);
    auto* sendLayout = new QVBoxLayout(sendPage);
    sendLayout->addWidget(m_sendList, 1);
    sendLayout->addWidget(choose);
    sendLayout->addWidget(make);
    sendLayout->addWidget(new QLabel(tr("코드"), sendPage));
    sendLayout->addWidget(m_sendCode);
    sendLayout->addWidget(m_sendStatus);

    m_instance = new QComboBox(this);
    m_receiveCode = new QLineEdit(this);
    m_receiveList = new QListWidget(this);
    m_receiveStatus = new QLabel(this);
    m_receiveStatus->setWordWrap(true);
    auto* lookup = new QPushButton(tr("목록 보기"), this);
    m_accept = new QPushButton(tr("수락하고 넣기"), this);
    m_accept->setEnabled(false);
    auto* receivePage = new QWidget(this);
    auto* receiveForm = new QFormLayout();
    receiveForm->addRow(tr("인스턴스"), m_instance);
    receiveForm->addRow(tr("코드"), m_receiveCode);
    auto* receiveLayout = new QVBoxLayout(receivePage);
    receiveLayout->addLayout(receiveForm);
    receiveLayout->addWidget(lookup);
    receiveLayout->addWidget(m_receiveList, 1);
    receiveLayout->addWidget(m_accept);
    receiveLayout->addWidget(m_receiveStatus);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(sendPage, tr("보내기"));
    tabs->addTab(receivePage, tr("받기"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(warning);
    layout->addWidget(tabs, 1);

    connect(choose, &QPushButton::clicked, this, &PackTransferDialog::chooseFiles);
    connect(make, &QPushButton::clicked, this, &PackTransferDialog::createOffer);
    connect(lookup, &QPushButton::clicked, this, &PackTransferDialog::lookupOffer);
    connect(m_accept, &QPushButton::clicked, this, &PackTransferDialog::acceptOffer);
    refreshInstances();
}

void PackTransferDialog::refreshInstances()
{
    m_instance->clear();
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        m_instance->addItem(instance->name(), instance->id());
    }
}

QString PackTransferDialog::selectedInstanceId() const
{
    return m_instance->currentData().toString();
}

void PackTransferDialog::chooseFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("jar 고르기"), {}, tr("모드 (*.jar)"));
    m_outgoing.clear();
    m_sendList->clear();
    for (const QString& path : paths) {
        QFileInfo info(path);
        if (!info.fileName().endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive))
            continue;
        OfferFile file;
        file.name = info.fileName();
        file.size = info.size();
        file.path = path;
        m_outgoing.append(file);
        m_sendList->addItem(QStringLiteral("%1  %2바이트").arg(file.name, QString::number(file.size)));
    }
}

void PackTransferDialog::createOffer()
{
    if (m_outgoing.isEmpty()) {
        m_sendStatus->setText(tr("보낼 jar를 고르세요."));
        return;
    }
    QJsonArray files;
    QJsonObject root;
    for (OfferFile& file : m_outgoing) {
        QFile input(file.path);
        if (!input.open(QIODevice::ReadOnly)) {
            m_sendStatus->setText(tr("파일을 열지 못했습니다: %1").arg(file.name));
            return;
        }
        const QByteArray bytes = input.readAll();
        file.sha256 = sha256Of(bytes);
        file.size = bytes.size();
        QJsonObject item;
        item.insert(QStringLiteral("name"), file.name);
        item.insert(QStringLiteral("size"), file.size);
        item.insert(QStringLiteral("sha256"), file.sha256);
        files.append(item);
    }
    root.insert(QStringLiteral("files"), files);
    QNetworkRequest request{QUrl(kRelayBase + QStringLiteral("/v1/offers"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_reply = m_network->post(request, QJsonDocument(root).toJson(QJsonDocument::Compact));
    m_sendStatus->setText(tr("코드를 만드는 중"));
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const QByteArray body = m_reply->readAll();
        const bool failed = m_reply->error() != QNetworkReply::NoError;
        m_reply->deleteLater();
        m_reply = nullptr;
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        if (failed) {
            m_sendStatus->setText(object.value(QStringLiteral("error")).toString(tr("중계에 연결하지 못했습니다.")));
            return;
        }
        m_sendCode->setText(object.value(QStringLiteral("code")).toString());
        m_token = object.value(QStringLiteral("token")).toString();
        m_index = 0;
        uploadNext();
    });
}

void PackTransferDialog::uploadNext()
{
    if (m_index >= m_outgoing.size()) {
        m_sendStatus->setText(tr("올렸습니다. 이 코드를 보내세요. 15분 뒤에 사라집니다."));
        return;
    }
    const OfferFile& file = m_outgoing.at(m_index);
    QFile input(file.path);
    if (!input.open(QIODevice::ReadOnly)) {
        m_sendStatus->setText(tr("파일을 열지 못했습니다: %1").arg(file.name));
        return;
    }
    QNetworkRequest request{QUrl(kRelayBase + QStringLiteral("/v1/offers/%1/%2").arg(m_sendCode->text(), QString::number(m_index)))};
    request.setRawHeader("X-Token", m_token.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/java-archive"));
    m_reply = m_network->post(request, input.readAll());
    m_sendStatus->setText(tr("올리는 중: %1").arg(file.name));
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const bool failed = m_reply->error() != QNetworkReply::NoError;
        m_reply->deleteLater();
        m_reply = nullptr;
        if (failed) {
            m_sendStatus->setText(tr("올리지 못했습니다."));
            return;
        }
        ++m_index;
        uploadNext();
    });
}

void PackTransferDialog::lookupOffer()
{
    const QString code = m_receiveCode->text().trimmed();
    if (code.isEmpty())
        return;
    m_accept->setEnabled(false);
    m_incoming.clear();
    m_receiveList->clear();
    m_reply = m_network->get(QNetworkRequest{QUrl(kRelayBase + QStringLiteral("/v1/offers/") + code)});
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const QByteArray body = m_reply->readAll();
        const bool failed = m_reply->error() != QNetworkReply::NoError;
        m_reply->deleteLater();
        m_reply = nullptr;
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        if (failed) {
            m_receiveStatus->setText(object.value(QStringLiteral("error")).toString(tr("코드를 찾지 못했습니다.")));
            return;
        }
        for (const QJsonValue& value : object.value(QStringLiteral("files")).toArray()) {
            const QJsonObject file = value.toObject();
            OfferFile item;
            item.name = file.value(QStringLiteral("name")).toString();
            item.size = file.value(QStringLiteral("size")).toInteger();
            item.sha256 = file.value(QStringLiteral("sha256")).toString();
            if (!item.name.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive))
                continue;
            m_incoming.append(item);
            m_receiveList->addItem(QStringLiteral("%1  %2바이트").arg(item.name, QString::number(item.size)));
        }
        m_accept->setEnabled(!m_incoming.isEmpty() && !selectedInstanceId().isEmpty());
        m_receiveStatus->setText(tr("파일을 확인한 뒤에 수락하세요."));
    });
}

void PackTransferDialog::acceptOffer()
{
    auto* instance = APPLICATION->instances()->getInstanceById(selectedInstanceId());
    if (!instance) {
        m_receiveStatus->setText(tr("인스턴스를 고르세요."));
        return;
    }
    if (!QDir().mkpath(instance->modsRoot())) {
        m_receiveStatus->setText(tr("모드 폴더를 만들지 못했습니다."));
        return;
    }
    m_accept->setEnabled(false);
    m_index = 0;
    downloadNext();
}

void PackTransferDialog::downloadNext()
{
    if (m_index >= m_incoming.size()) {
        auto* instance = APPLICATION->instances()->getInstanceById(selectedInstanceId());
        if (instance)
            instance->loaderModList()->update();
        m_receiveStatus->setText(tr("%1개 jar를 넣었습니다.").arg(m_incoming.size()));
        return;
    }
    const QString code = m_receiveCode->text().trimmed();
    m_reply = m_network->get(QNetworkRequest{QUrl(kRelayBase + QStringLiteral("/v1/offers/%1/%2").arg(code, QString::number(m_index)))});
    m_receiveStatus->setText(tr("받는 중: %1").arg(m_incoming.at(m_index).name));
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const QByteArray body = m_reply->readAll();
        const bool failed = m_reply->error() != QNetworkReply::NoError;
        m_reply->deleteLater();
        m_reply = nullptr;
        if (failed) {
            m_receiveStatus->setText(tr("받지 못했습니다."));
            return;
        }
        finishDownload(body);
    });
}

void PackTransferDialog::finishDownload(const QByteArray& body)
{
    const OfferFile& file = m_incoming.at(m_index);
    if (sha256Of(body) != file.sha256 || body.size() != file.size) {
        m_receiveStatus->setText(tr("파일이 코드의 내용과 다릅니다. 넣지 않았습니다: %1").arg(file.name));
        return;
    }
    auto* instance = APPLICATION->instances()->getInstanceById(selectedInstanceId());
    if (!instance) {
        m_receiveStatus->setText(tr("인스턴스를 찾지 못했습니다."));
        return;
    }
    QSaveFile output(QDir(instance->modsRoot()).filePath(file.name));
    if (!output.open(QIODevice::WriteOnly) || output.write(body) != body.size() || !output.commit()) {
        m_receiveStatus->setText(tr("파일을 쓰지 못했습니다: %1").arg(file.name));
        return;
    }
    ++m_index;
    downloadNext();
}

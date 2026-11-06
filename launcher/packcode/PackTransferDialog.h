// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;

class PackTransferDialog : public QDialog {
    Q_OBJECT
   public:
    explicit PackTransferDialog(QWidget* parent = nullptr);

   private:
    struct OfferFile {
        QString name;
        qint64 size = 0;
        QString sha256;
        QString path;
    };

    void refreshInstances();
    void chooseFiles();
    void createOffer();
    void uploadNext();
    void lookupOffer();
    void acceptOffer();
    void downloadNext();
    void finishDownload(const QByteArray& body);
    QString selectedInstanceId() const;

    QNetworkAccessManager* m_network = nullptr;
    QListWidget* m_sendList = nullptr;
    QListWidget* m_receiveList = nullptr;
    QLineEdit* m_sendCode = nullptr;
    QLineEdit* m_receiveCode = nullptr;
    QComboBox* m_instance = nullptr;
    QLabel* m_sendStatus = nullptr;
    QLabel* m_receiveStatus = nullptr;
    QPushButton* m_accept = nullptr;
    QList<OfferFile> m_outgoing;
    QList<OfferFile> m_incoming;
    QString m_token;
    int m_index = 0;
    QNetworkReply* m_reply = nullptr;
};

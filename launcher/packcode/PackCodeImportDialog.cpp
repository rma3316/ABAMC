// SPDX-License-Identifier: GPL-3.0-only
#include "PackCodeImportDialog.h"

#include "packcode/PackCodeCreationTask.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

PackCodeImportDialog::PackCodeImportDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("코드로 추가"));
    resize(560, 460);

    m_code = new QPlainTextEdit(this);
    m_code->setPlaceholderText(tr("pc1. 으로 시작하는 코드를 붙여 넣으세요."));
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_reason = new QLabel(this);
    m_reason->setWordWrap(true);
    m_mods = new QListWidget(this);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_create = buttons->addButton(tr("인스턴스 만들기"), QDialogButtonBox::AcceptRole);
    m_create->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("코드"), this));
    layout->addWidget(m_code, 1);
    layout->addWidget(m_summary);
    layout->addWidget(m_reason);
    layout->addWidget(new QLabel(tr("모드"), this));
    layout->addWidget(m_mods, 1);
    layout->addWidget(buttons);

    connect(m_code, &QPlainTextEdit::textChanged, this, &PackCodeImportDialog::refresh);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, &QDialog::accept);
}

InstanceTask* PackCodeImportDialog::extractTask()
{
    if (!m_preset)
        return nullptr;
    auto* task = new PackCodeCreationTask(*m_preset);
    task->setName(QString::fromStdString(m_preset->name));
    return task;
}

void PackCodeImportDialog::refresh()
{
    m_mods->clear();
    m_preset.reset();
    m_create->setEnabled(false);
    const QString text = m_code->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_summary->clear();
        m_reason->setText(tr("코드를 붙여 넣으면 이름, 버전, 로더, 서버가 여기에 나옵니다."));
        return;
    }
    try {
        PackCode::Preset preset = PackCode::decode(text.toStdString());
        QString loader = QString::fromStdString(PackCode::loaderToJson(preset.loader));
        if (preset.loaderVersion)
            loader += " " + QString::fromStdString(*preset.loaderVersion);
        const QString server = preset.server ? QString::fromStdString(preset.server->address) : tr("없음");
        m_summary->setText(tr("이름  %1\n마크  %2\n로더  %3\n서버  %4")
                               .arg(QString::fromStdString(preset.name), QString::fromStdString(preset.minecraftVersion), loader, server));
        QString reason;
        if (!preset.loaderVersion)
            reason = tr("로더 버전이 없습니다.");
        else if (preset.loader == PackCode::ModLoader::NeoForge && !PackCode::minecraftAtLeast121(preset.minecraftVersion))
            reason = tr("NeoForge는 마크 1.21 이상입니다.");
        for (const auto& mod : preset.mods) {
            const QString version = mod.versionId ? QString::fromStdString(*mod.versionId) : tr("버전 없음");
            m_mods->addItem(QString::fromStdString(mod.projectId) + "    " + version);
            if (!mod.versionId && reason.isEmpty())
                reason = tr("모드 버전이 고정되어 있지 않습니다: %1").arg(QString::fromStdString(mod.projectId));
        }
        m_reason->setText(reason);
        if (reason.isEmpty()) {
            m_preset = preset;
            m_create->setEnabled(true);
        }
    } catch (const PackCode::Error& error) {
        m_summary->clear();
        m_reason->setText(QString::fromUtf8(error.what()));
    }
}

// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>

#include <QDialog>

#include "packcode/PackPreset.h"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class InstanceTask;

class PackCodeImportDialog : public QDialog {
    Q_OBJECT
   public:
    explicit PackCodeImportDialog(QWidget* parent = nullptr);

    InstanceTask* extractTask();

   private:
    void refresh();

    QPlainTextEdit* m_code = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_reason = nullptr;
    QListWidget* m_mods = nullptr;
    QPushButton* m_create = nullptr;
    std::optional<PackCode::Preset> m_preset;
};

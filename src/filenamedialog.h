// Copyright (c) 2023-2025 Manuel Schneider

#pragma once
#include <QDialog>
#include <QDir>
class QLabel;
class QLineEdit;
class QDialogButtonBox;

class FilenameDialog : public QDialog
{
    Q_OBJECT

public:

    FilenameDialog(QDir loc, QWidget* parent = nullptr);
    QString name();
    QString filePath();
    void updateUI(const QString &text);
    void accept() override;

private:

    QDir snippets_dir;
    QLabel *label, *info_label;
    QLineEdit *line_edit;
    QDialogButtonBox *buttons;

};

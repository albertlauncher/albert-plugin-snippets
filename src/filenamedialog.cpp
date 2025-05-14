// Copyright (c) 2023-2025 Manuel Schneider

#include "filenamedialog.h"
#include <QFile>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
using namespace std;

FilenameDialog::FilenameDialog(QDir loc, QWidget *parent) :
    QDialog(parent),
    snippets_dir(loc)
{
    label = new QLabel(tr("Snippet name:"), this);
    info_label = new QLabel(this);
    line_edit = new QLineEdit(this);
    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    connect(buttons, &QDialogButtonBox::accepted, this, &FilenameDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &FilenameDialog::reject);
    connect(line_edit, &QLineEdit::textChanged, this, &FilenameDialog::updateUI);

    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->addWidget(label);
    layout->addWidget(line_edit);
    layout->addWidget(info_label);
    layout->addWidget(buttons);

    updateUI({});
}

QString FilenameDialog::name() { return line_edit->text(); }

QString FilenameDialog::filePath() { return snippets_dir.filePath(name()) + ".txt"; }

void FilenameDialog::updateUI(const QString &text)
{
    if (text.isEmpty())
    {
        info_label->setText(tr("The snippet name must not be empty."));
        info_label->show();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    else if (QFile::exists(snippets_dir.filePath(text) + ".txt"))
    {
        info_label->setText(tr("There is already a snippet called '%1'.").arg(text));
        info_label->show();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    else
    {
        info_label->clear();
        info_label->hide();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
}

void FilenameDialog::accept()
{
    if (!name().isEmpty() && !QFile::exists(filePath()))
        QDialog::accept();
}

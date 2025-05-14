// Copyright (c) 2023-2025 Manuel Schneider

#include "plugin.h"
#include "filenamedialog.h"
#include "ui_configwidget.h"
#include <QFile>
#include <QFileSystemModel>
#include <QMessageBox>
#include <QTextStream>
#include <albert/albert.h>
#include <albert/widgetsutil.h>
#include <albert/standarditem.h>
#include <QTimer>
#include <memory>
ALBERT_LOGGING_CATEGORY("snippets")
using namespace albert::util;
using namespace albert;
using namespace std;

static const auto preview_max_size = 100;
static const auto prefix_add = QString("+");

struct SnippetItem : Item
{
    SnippetItem(const QFileInfo& fi, Plugin *p)
        : file_base_name_(fi.completeBaseName()), plugin_(p)
    {
        QFile file(fi.filePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            preview_ = in.readAll().simplified();
            if (preview_.size() > preview_max_size)
                preview_ = preview_.left(preview_max_size) + " …";
            preview_.squeeze();
            file.close();
        } else
            WARN << "Failed to read from snippet file" << path();
    }

    QString id() const override { return file_base_name_; }

    QString text() const override { return file_base_name_; }

    QString subtext() const override
    {
        static const auto tr = Plugin::tr("Text snippet");
        return QString("%1 – %2").arg(tr, preview_);
    }

    QStringList iconUrls() const override { return {":snippet"}; }

    vector<Action> actions() const override
    {
        vector<Action> actions;

        if (havePasteSupport())
            actions.emplace_back(
                "cp", Plugin::tr("Copy and paste"),
                [this]{
                    QFile f(path());
                    f.open(QIODevice::ReadOnly);
                    setClipboardTextAndPaste(QTextStream(&f).readAll());
                }
            );

        actions.emplace_back(
            "c", Plugin::tr("Copy"),
            [this]{
                QFile f(path());
                f.open(QIODevice::ReadOnly);
                setClipboardText(QTextStream(&f).readAll());
            }
        );

        actions.emplace_back("o", Plugin::tr("Edit"), [this]{ open(path()); });

        actions.emplace_back("r", Plugin::tr("Remove"),
                             [this]{ plugin_->removeSnippet(file_base_name_+".txt"); });

        return actions;
    }

    QString path() const
    { return QDir(plugin_->configLocation()).filePath(file_base_name_ + ".txt"); }

private:

    const QString file_base_name_;
    QString preview_;
    Plugin * const plugin_;
};


Plugin::Plugin()
{
    const auto conf_path = configLocation();

    tryCreateDirectory(conf_path);

    fs_watcher.addPath(conf_path.c_str());
    connect(&fs_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this]{ updateIndexItems(); });

    indexer.parallel = [this](const bool &abort){
        vector<IndexItem> r;
        for (const auto &f : QDir(configLocation()).entryInfoList({"*.txt"}, QDir::Files)){
            if (abort) return r;
            r.emplace_back(make_shared<SnippetItem>(f, this), f.completeBaseName());
        }
        return r;
    };
    indexer.finish = [this](vector<IndexItem> &&results){
        setIndexItems(::move(results));
    };
}

QString Plugin::defaultTrigger() const { return "snip "; }

QString Plugin::synopsis(const QString &q) const
{
    static const auto tr_s = tr("<filter>|+");
    static const auto tr_sa = tr("[snippet text]");

    if (q.startsWith(prefix_add))
        return tr_sa;
    else
        return tr_s;
}

void Plugin::updateIndexItems() { indexer.run(); }

void Plugin::handleTriggerQuery(Query &query)
{
    if (query.string().startsWith(prefix_add))
        query.add(
            StandardItem::make(
                prefix_add,
                tr("Create new snippet"),
                tr("Create snippet file and open it in default editor."),
                {":snippet"},
                {{
                    "add", tr("Create"),
                    [this, q=query.string().mid(prefix_add.size())]{ addSnippet(q); }
                }}
            )
        );
    else
        IndexQueryHandler::handleTriggerQuery(query);
}

void Plugin::addSnippet(const QString &text, QWidget *parent) const
{
    if (!parent)
    {
        showSettings(id()); // sets config_widget
        parent = config_widget;
    }

    auto dialog = new FilenameDialog(QDir(configLocation()), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();

    connect(dialog, &QDialog::finished, this, [text, dialog](int result){
        if (result == QDialog::Accepted)
        {
            if (QFile file(dialog->filePath());
                file.open(QIODevice::WriteOnly))
            {
                if (text.isEmpty())
                    open(file.fileName());
                else
                    QTextStream(&file) << text;
                file.close();
            }
            else
                critical(tr("Failed creating the snippet file '%1'.").arg(file.fileName()));
        }
        dialog->deleteLater();
    });

    // The following old code is extremely fragile due to the non activating nature of the app
    // for now simply showing the settings makes sure the app has key

    // if (parent)
    // {
    //     // dialog->setWindowModality(Qt::WindowModal);
    //
    // }
    // else
    // {
    //     // dialog->setWindowModality(Qt::ApplicationModal);
    //     // dialog->setModal(true);
    //     dialog->show();
    //     dialog->raise();
    //     dialog->activateWindow();
    // }

    // CRIT << dialog->windowModality();
    // connect(dialog, &QWidget::destroyed, this, [](){ CRIT << "destroyed";});

}

void Plugin::removeSnippet(const QString &file_name) const
{
    auto path = QDir(configLocation()).filePath(file_name);
    if (!QFile::exists(path))
        WARN << "Path to remove does not exist:" << path;
    else if (question(tr("Move snippet '%1' to trash?").arg(file_name)) == QMessageBox::Yes)
        if (!QFile::moveToTrash(path))
            warning(tr("Failed to move snippet file to trash."));
}

class RedIfNotTxtFileSystemModel : public QFileSystemModel
{
public:
    RedIfNotTxtFileSystemModel(QObject *parent) : QFileSystemModel(parent){}
    virtual QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const {
        if (role == Qt::ForegroundRole && !index.data().toString().endsWith(".txt"))
            return QColorConstants::Red;
        else
            return QFileSystemModel::data(index, role);
    }
};


QWidget *Plugin::buildConfigWidget()
{
    config_widget = new QWidget;
    Ui::ConfigWidget ui;
    ui.setupUi(config_widget);

    auto *model = new RedIfNotTxtFileSystemModel(ui.listView);
    model->setFilter(QDir::Files);
    model->setReadOnly(false);
    model->setRootPath(configLocation().c_str());

    ui.listView->setModel(model);
    ui.listView->setRootIndex(model->index(model->rootPath()));

    connect(ui.listView, &QListView::activated, this,
            [model](const QModelIndex &index){ open(model->filePath(index)); });

    connect(ui.pushButton_opendir, &QPushButton::clicked, this,
            [this](){ open(configLocation()); });

    connect(ui.pushButton_add, &QPushButton::clicked,
            this, [this](){ addSnippet({}, config_widget); });

    connect(ui.pushButton_remove, &QPushButton::clicked, this,
            [this, model, lw=ui.listView](){
        if (lw->currentIndex().isValid())
            removeSnippet(model->filePath(lw->currentIndex()));
    });

    return config_widget;
}

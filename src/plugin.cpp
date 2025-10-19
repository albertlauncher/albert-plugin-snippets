// Copyright (c) 2023-2025 Manuel Schneider

#include "filenamedialog.h"
#include "plugin.h"
#include "ui_configwidget.h"
#include <QFile>
#include <QFileSystemModel>
#include <QTextStream>
#include <QTimer>
#include <albert/app.h>
#include <albert/iconutil.h>
#include <albert/messagebox.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <albert/logging.h>
ALBERT_LOGGING_CATEGORY("snippets")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

static const auto preview_max_size = 100;
static const auto prefix_add = u"+"_s;
static unique_ptr<Icon> makeIcon() { return makeImageIcon(u":snippet"_s); }

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
                preview_ = preview_.left(preview_max_size) + u" …"_s;
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
        return u"%1 – %2"_s.arg(tr, preview_);
    }

    unique_ptr<Icon> icon() const override { return ::makeIcon(); }

    QString path() const
    { return QDir(plugin_->configLocation()).filePath(file_base_name_ + u".txt"_s); }

    static void onReadFailed(const QString &path, const QString &error)
    {
        auto *msg = QT_TRANSLATE_NOOP("Plugin", "Failed to read snippet file '%1'. Error: %2");
        WARN << QString::fromUtf8(msg).arg(path, error);
        warning(Plugin::tr(msg).arg(path, error));
    }

    void copyToClipboard() const
    {
        QFile f(path());
        if (f.open(QIODevice::ReadOnly))
            setClipboardText(QTextStream(&f).readAll());
        else
            onReadFailed(path(), f.errorString());
    }

    void copyToClipboardAndPaste() const
    {
        QFile f(path());
        if (f.open(QIODevice::ReadOnly))
            setClipboardTextAndPaste(QTextStream(&f).readAll());
        else
            onReadFailed(path(), f.errorString());
    }

    vector<Action> actions() const override
    {
        vector<Action> actions;

        if (havePasteSupport())
            actions.emplace_back(u"cp"_s, Plugin::tr("Copy and paste"),
                                 [this]{ copyToClipboardAndPaste(); });

        actions.emplace_back(u"c"_s, Plugin::tr("Copy"), [this]{ copyToClipboard(); });

        actions.emplace_back(u"o"_s, Plugin::tr("Edit"), [this]{ open(path()); });

        actions.emplace_back(u"r"_s, Plugin::tr("Remove"),
                             [this]{ plugin_->removeSnippet(file_base_name_ + u".txt"_s); });

        return actions;
    }

private:

    const QString file_base_name_;
    QString preview_;
    Plugin * const plugin_;
};


Plugin::Plugin()
{
    const auto conf_path = configLocation();

    filesystem::create_directories(conf_path);

    fs_watcher.addPath(QString::fromLocal8Bit(conf_path.c_str()));
    connect(&fs_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this]{ updateIndexItems(); });

    indexer.parallel = [this](const bool &abort)
    {
        vector<IndexItem> r;
        for (const auto files = QDir(configLocation()).entryInfoList({u"*.txt"_s}, QDir::Files);
             const auto &f : files)
        {
            if (abort) return r;
            r.emplace_back(make_shared<SnippetItem>(f, this), f.completeBaseName());
        }
        return r;
    };

    indexer.finish = [this]
    {
        auto index_items = indexer.takeResult();
        INFO << u"Indexed %1 snippets."_s.arg(index_items.size());
        setIndexItems(::move(index_items));
    };
}

QString Plugin::defaultTrigger() const { return u"snip "_s; }

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

vector<RankItem> Plugin::rankItems(QueryContext &ctx)
{
    vector<RankItem> results = IndexQueryHandler::rankItems(ctx);

    if (ctx.query().startsWith(prefix_add))
        results.emplace_back(
            StandardItem::make(
                prefix_add,
                tr("Create new snippet"),
                tr("Create snippet file and open it in default editor."),
                makeIcon,
                {{
                    u"add"_s, tr("Create"),
                    [this, q=ctx.query().mid(prefix_add.size())]{ addSnippet(q); }
                }}
            ),
            1.
        );

    return results;
}

void Plugin::addSnippet(const QString &text, QWidget *parent) const
{
    if (!parent)
    {
        App::instance().showSettings(id()); // sets config_widget
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
    else if (question(tr("Move snippet '%1' to trash?").arg(file_name)))
        if (!QFile::moveToTrash(path))
            warning(tr("Failed to move snippet file to trash."));
}

class RedIfNotTxtFileSystemModel : public QFileSystemModel
{
public:
    RedIfNotTxtFileSystemModel(QObject *parent) : QFileSystemModel(parent){}
    virtual QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const {
        if (role == Qt::ForegroundRole && !index.data().toString().endsWith(u".txt"_s))
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
    model->setRootPath(QString::fromLocal8Bit(configLocation().c_str()));

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

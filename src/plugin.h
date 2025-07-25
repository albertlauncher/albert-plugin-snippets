// Copyright (c) 2023-2025 Manuel Schneider

#pragma once

#include "snippets.h"
#include <QFileSystemWatcher>
#include <albert/backgroundexecutor.h>
#include <albert/extensionplugin.h>
#include <albert/indexqueryhandler.h>
class QWidget;

class Plugin : public albert::util::ExtensionPlugin,
               public albert::util::IndexQueryHandler,
               public snippets::Plugin

{
    ALBERT_PLUGIN
public:

    Plugin();

    void addSnippet(const QString &text = {}, QWidget *modal_parent = nullptr) const override;
    void removeSnippet(const QString &file_name) const;

private:

    QString defaultTrigger() const override;
    QWidget* buildConfigWidget() override;
    void updateIndexItems() override;
    QString synopsis(const QString &) const override;
    void handleTriggerQuery(albert::Query &) override;

    QWidget *config_widget = nullptr;
    QFileSystemWatcher fs_watcher;
    albert::util::BackgroundExecutor<std::vector<albert::util::IndexItem>> indexer;

};

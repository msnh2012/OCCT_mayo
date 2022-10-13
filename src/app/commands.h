/****************************************************************************
** Copyright (c) 2022, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "../base/document.h"
#include "../base/filepath.h"
#include "../base/span.h"
#include "../base/text_id.h"

#include <QtCore/QObject>
#include <QtGui/QAction>
class QWidget;

namespace Mayo {

class Application;
class GuiApplication;
class GuiDocument;
class TaskManager;

class IAppContext : public QObject {
    Q_OBJECT
public:
    virtual GuiApplication* guiApp() const = 0;
    virtual TaskManager* taskMgr() const = 0;
    virtual QWidget* mainWidget() const = 0;

    virtual Document::Identifier currentDocument() const = 0;
    virtual void setCurrentDocument(Document::Identifier docId) = 0;

    virtual void updateControlsEnabledStatus() = 0;
    virtual void deleteDocumentWidget(const DocumentPtr& doc) = 0;

signals:
    void currentDocumentChanged(Mayo::Document::Identifier docId);
};

class Command : public QObject {
    Q_OBJECT
    MAYO_DECLARE_TEXT_ID_FUNCTIONS(Mayo::Command)
public:
    Command(IAppContext* context);
    virtual ~Command() = default;

    virtual void execute() = 0;

    IAppContext* context() const { return m_context; }

    QAction* action() const { return m_action; }
    virtual bool getEnabledStatus() const { return true; }

    template<typename CMD>
    static CMD* create(IAppContext* context) {
        auto cmd = new CMD(context);
        QObject::connect(cmd->action(), &QAction::triggered, cmd, &Command::execute);
        return cmd;
    }

protected:
    Application* app() const;
    GuiApplication* guiApp() const { return m_context->guiApp(); }
    TaskManager* taskMgr() const { return m_context->taskMgr(); }
    QWidget* mainWidget() const { return m_context->mainWidget(); }
    Document::Identifier currentDocument() const { return m_context->currentDocument(); }
    GuiDocument* currentGuiDocument() const;

    void setCurrentDocument(const DocumentPtr& doc);
    void setAction(QAction* action);

private:
    IAppContext* m_context = nullptr;
    QAction* m_action = nullptr;
};

// --
// -- "File" commands
// --

class CommandNewDocument : public Command {
public:
    CommandNewDocument(IAppContext* context);
    void execute() override;
};

class CommandOpenDocuments : public Command {
public:
    CommandOpenDocuments(IAppContext* context);
    void execute() override;
    void openDocumentsFromList(Span<const FilePath> listFilePath);
};

class CommandImportInCurrentDocument : public Command {
public:
    CommandImportInCurrentDocument(IAppContext* context);
    void execute() override;
    bool getEnabledStatus() const override;
};

class CommandExportSelectedApplicationItems : public Command {
public:
    CommandExportSelectedApplicationItems(IAppContext* context);
    void execute() override;
    bool getEnabledStatus() const override;
};

class CommandCloseCurrentDocument : public Command {
public:
    CommandCloseCurrentDocument(IAppContext* context);
    void execute() override;
    bool getEnabledStatus() const override;

private:
    void updateActionText(Document::Identifier docId);
};

// --
// -- Window command
// --

class CommandMainWidgetToggleFullscreen : public Command {
public:
    CommandMainWidgetToggleFullscreen(IAppContext* context);
    void execute() override;

private:
    Qt::WindowStates m_previousWindowState = Qt::WindowNoState;
};

} // namespace Mayo

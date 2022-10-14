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
#include <QAction> // WARNING Qt5 <QtWidgets/...> / Qt6 <QtGui/...>
class QWidget;

namespace Mayo {

class Application;
class GuiApplication;
class GuiDocument;
class TaskManager;

class IAppContext : public QObject {
    Q_OBJECT
public:
    IAppContext(QObject* parent = nullptr);

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

class FileCommandTools {
public:
    static void closeDocument(IAppContext* context, Document::Identifier docId);
    static void openDocumentsFromList(IAppContext* context, Span<const FilePath> listFilePath);
    static void openDocument(IAppContext* context, FilePath fp);
};

class CommandNewDocument : public Command {
public:
    CommandNewDocument(IAppContext* context);
    void execute() override;
};

class CommandOpenDocuments : public Command {
public:
    CommandOpenDocuments(IAppContext* context);
    void execute() override;
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

class CommandCloseAllDocuments : public Command {
public:
    CommandCloseAllDocuments(IAppContext* context);
    void execute() override;
    bool getEnabledStatus() const override;
};

class CommandCloseAllDocumentsExceptCurrent : public Command {
public:
    CommandCloseAllDocumentsExceptCurrent(IAppContext* context);
    void execute() override;
    bool getEnabledStatus() const override;

private:
    void updateActionText(Document::Identifier docId);
};

class CommandRecentFiles : public Command {
public:
    CommandRecentFiles(IAppContext* context);
    CommandRecentFiles(IAppContext* context, QMenu* containerMenu);
    void execute() override;
    void recreateEntries();
};

class CommandQuitApplication : public Command {
public:
    CommandQuitApplication(IAppContext* context);
    void execute() override;
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

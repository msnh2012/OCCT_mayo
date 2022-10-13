/****************************************************************************
** Copyright (c) 2022, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "commands.h"

#include "../base/application.h"
#include "../base/task_manager.h"
#include "../gui/gui_application.h"
#include "app_module.h"
#include "filepath_conv.h"
#include "qstring_conv.h"
#include "theme.h"

#include <fmt/format.h>
#include <QtCore/QtDebug>
#include <QtCore/QElapsedTimer>
#include <QtWidgets/QFileDialog>

namespace Mayo {

namespace {

QString fileFilter(IO::Format format)
{
    if (format == IO::Format_Unknown)
        return {};

    QString filter;
    for (std::string_view suffix : IO::formatFileSuffixes(format)) {
        if (suffix.data() != IO::formatFileSuffixes(format).front().data())
            filter += " ";

        const QString qsuffix = to_QString(suffix);
        filter += "*." + qsuffix;
#ifdef Q_OS_UNIX
        filter += " *." + qsuffix.toUpper();
#endif
    }

    //: %1 is the format identifier and %2 is the file filters string
    return Command::tr("%1 files(%2)")
            .arg(to_QString(IO::formatIdentifier(format)))
            .arg(filter);
}

IO::Format formatFromFilter(const QString& filter)
{
    for (IO::Format format : AppModule::get()->ioSystem()->readerFormats()) {
        if (filter == fileFilter(format))
            return format;
    }

    for (IO::Format format : AppModule::get()->ioSystem()->writerFormats()) {
        if (filter == fileFilter(format))
            return format;
    }

    return IO::Format_Unknown;
}

// TODO: move in Options
struct ImportExportSettings {
    FilePath openDir;
    QString selectedFilter;

    static ImportExportSettings load()
    {
        return {
            AppModule::get()->properties()->lastOpenDir.value(),
            to_QString(AppModule::get()->properties()->lastSelectedFormatFilter.value())
        };
    }

    static void save(const ImportExportSettings& sets)
    {
        AppModule::get()->properties()->lastOpenDir.setValue(sets.openDir);
        AppModule::get()->properties()->lastSelectedFormatFilter.setValue(to_stdString(sets.selectedFilter));
    }
};

struct OpenFileNames {
    std::vector<FilePath> listFilepath;
    ImportExportSettings lastIoSettings;
    IO::Format selectedFormat;

    enum GetOption {
        GetOne,
        GetMany
    };

    static OpenFileNames get(
            QWidget* parentWidget,
            OpenFileNames::GetOption option = OpenFileNames::GetMany)
    {
        OpenFileNames result;
        result.selectedFormat = IO::Format_Unknown;
        result.lastIoSettings = ImportExportSettings::load();
        QStringList listFormatFilter;
        for (IO::Format format : AppModule::get()->ioSystem()->readerFormats())
            listFormatFilter += fileFilter(format);

        const QString allFilesFilter = Command::tr("All files(*.*)");
        listFormatFilter.append(allFilesFilter);
        const QString dlgTitle = Command::tr("Select Part File");
        const QString dlgOpenDir = filepathTo<QString>(result.lastIoSettings.openDir);
        const QString dlgFilter = listFormatFilter.join(QLatin1String(";;"));
        QString* dlgPtrSelFilter = &result.lastIoSettings.selectedFilter;
        if (option == OpenFileNames::GetOne) {
            const QString strFilepath =
                    QFileDialog::getOpenFileName(
                        parentWidget, dlgTitle, dlgOpenDir, dlgFilter, dlgPtrSelFilter);
            result.listFilepath.clear();
            result.listFilepath.push_back(filepathFrom(strFilepath));
        }
        else {
            const QStringList listStrFilePath =
                    QFileDialog::getOpenFileNames(
                        parentWidget, dlgTitle, dlgOpenDir, dlgFilter, dlgPtrSelFilter);
            result.listFilepath.clear();
            for (const QString& strFilePath : listStrFilePath)
                result.listFilepath.push_back(filepathFrom(strFilePath));
        }

        if (!result.listFilepath.empty()) {
            result.lastIoSettings.openDir = result.listFilepath.front();
            result.selectedFormat =
                    result.lastIoSettings.selectedFilter != allFilesFilter ?
                        formatFromFilter(result.lastIoSettings.selectedFilter) :
                        IO::Format_Unknown;
            ImportExportSettings::save(result.lastIoSettings);
        }

        return result;
    }
};

QString strFilepathQuoted(const QString& filepath)
{
    for (QChar c : filepath) {
        if (c.isSpace())
            return "\"" + filepath + "\"";
    }

    return filepath;
}

void closeDocument(IAppContext* context, Document::Identifier docId)
{
    auto app = context->guiApp()->application();
    DocumentPtr doc = app->findDocumentByIdentifier(docId);
    context->deleteDocumentWidget(doc);
    app->closeDocument(doc);
    context->updateControlsEnabledStatus();
}

} // namespace

Command::Command(IAppContext* context)
    : QObject(context ? context->mainWidget() : nullptr),
      m_context(context)
{
}

Application* Command::app() const
{
    return m_context->guiApp()->application().get();
}

GuiDocument* Command::currentGuiDocument() const
{
    DocumentPtr doc = this->app()->findDocumentByIdentifier(this->currentDocument());
    return this->guiApp()->findGuiDocument(doc);
}

void Command::setCurrentDocument(const DocumentPtr& doc)
{
    m_context->setCurrentDocument(doc->identifier());
}

void Command::setAction(QAction* action)
{
    m_action = action;
    QObject::connect(action, &QAction::triggered, this, &Command::execute);
}

CommandNewDocument::CommandNewDocument(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("New"));
    action->setToolTip(Command::tr("New Document"));
    action->setShortcut(Qt::CTRL + Qt::Key_N);
    this->setAction(action);
}

void CommandNewDocument::execute()
{
    static unsigned docSequenceId = 0;
    auto docPtr = this->app()->newDocument(Document::Format::Binary);
    docPtr->setName(to_stdString(Command::tr("Anonymous%1").arg(++docSequenceId)));
}

CommandOpenDocuments::CommandOpenDocuments(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("Open"));
    action->setToolTip(Command::tr("Open Documents"));
    action->setShortcut(Qt::CTRL + Qt::Key_O);
    this->setAction(action);
}

void CommandOpenDocuments::execute()
{
    const auto resFileNames = OpenFileNames::get(this->mainWidget());
    if (!resFileNames.listFilepath.empty())
        this->openDocumentsFromList(resFileNames.listFilepath);
}

void CommandOpenDocuments::openDocumentsFromList(Span<const FilePath> listFilePath)
{
    auto appModule = AppModule::get();
    for (const FilePath& fp : listFilePath) {
        DocumentPtr docPtr = this->app()->findDocumentByLocation(fp);
        if (docPtr.IsNull()) {
            docPtr = this->app()->newDocument();
            const TaskId taskId = this->taskMgr()->newTask([=](TaskProgress* progress) {
                QElapsedTimer chrono;
                chrono.start();
                docPtr->setName(fp.stem().u8string());
                docPtr->setFilePath(fp);

                const bool okImport =
                        appModule->ioSystem()->importInDocument()
                        .targetDocument(docPtr)
                        .withFilepath(fp)
                        .withParametersProvider(appModule)
                        .withEntityPostProcess([=](TDF_Label labelEntity, TaskProgress* progress) {
                            appModule->computeBRepMesh(labelEntity, progress);
                        })
                        .withEntityPostProcessRequiredIf(&IO::formatProvidesBRep)
                        .withEntityPostProcessInfoProgress(20, Command::textIdTr("Mesh BRep shapes"))
                        .withMessenger(appModule)
                        .withTaskProgress(progress)
                        .execute();
                if (okImport)
                    appModule->emitInfo(fmt::format(Command::textIdTr("Import time: {}ms"), chrono.elapsed()));
            });
            this->taskMgr()->setTitle(taskId, fp.stem().u8string());
            this->taskMgr()->run(taskId);
            appModule->prependRecentFile(fp);
        }
        else {
            if (listFilePath.size() == 1)
                this->setCurrentDocument(docPtr);
        }
    } // endfor()
}

CommandImportInCurrentDocument::CommandImportInCurrentDocument(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("Import"));
    action->setToolTip(Command::tr("Import in current document"));
    action->setIcon(mayoTheme()->icon(Theme::Icon::Import));
    this->setAction(action);
}

void CommandImportInCurrentDocument::execute()
{
    const GuiDocument* guiDoc = this->currentGuiDocument();
    if (!guiDoc)
        return;

    const auto resFileNames = OpenFileNames::get(this->mainWidget());
    if (resFileNames.listFilepath.empty())
        return;

    auto appModule = AppModule::get();
    const TaskId taskId = this->taskMgr()->newTask([=](TaskProgress* progress) {
        QElapsedTimer chrono;
        chrono.start();

        const bool okImport = appModule->ioSystem()->importInDocument()
                .targetDocument(guiDoc->document())
                .withFilepaths(resFileNames.listFilepath)
                .withParametersProvider(appModule)
                .withEntityPostProcess([=](TDF_Label labelEntity, TaskProgress* progress) {
                        appModule->computeBRepMesh(labelEntity, progress);
                })
                .withEntityPostProcessRequiredIf(&IO::formatProvidesBRep)
                .withEntityPostProcessInfoProgress(20, Command::textIdTr("Mesh BRep shapes"))
                .withMessenger(appModule)
                .withTaskProgress(progress)
                .execute();
        if (okImport)
            appModule->emitInfo(fmt::format(Command::textIdTr("Import time: {}ms"), chrono.elapsed()));
    });
    const QString taskTitle =
            resFileNames.listFilepath.size() > 1 ?
                Command::tr("Import") :
                filepathTo<QString>(resFileNames.listFilepath.front().stem());
    this->taskMgr()->setTitle(taskId, to_stdString(taskTitle));
    this->taskMgr()->run(taskId);
    for (const FilePath& fp : resFileNames.listFilepath)
        appModule->prependRecentFile(fp);
}

bool CommandImportInCurrentDocument::getEnabledStatus() const
{
    return this->app()->documentCount() != 0;
}

CommandExportSelectedApplicationItems::CommandExportSelectedApplicationItems(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("Export selected items"));
    action->setToolTip(Command::tr("Export selected items"));
    action->setIcon(mayoTheme()->icon(Theme::Icon::Export));
    this->setAction(action);
}

void CommandExportSelectedApplicationItems::execute()
{
    auto appModule = AppModule::get();
    QStringList listWriterFileFilter;
    for (IO::Format format : appModule->ioSystem()->writerFormats())
        listWriterFileFilter.append(fileFilter(format));

    auto lastSettings = ImportExportSettings::load();
    const QString strFilepath =
            QFileDialog::getSaveFileName(
                this->mainWidget(),
                Command::tr("Select Output File"),
                filepathTo<QString>(lastSettings.openDir),
                listWriterFileFilter.join(QLatin1String(";;")),
                &lastSettings.selectedFilter);
    if (strFilepath.isEmpty())
        return;

    lastSettings.openDir = filepathFrom(strFilepath);
    const IO::Format format = formatFromFilter(lastSettings.selectedFilter);
    const TaskId taskId = this->taskMgr()->newTask([=](TaskProgress* progress) {
        QElapsedTimer chrono;
        chrono.start();
        const bool okExport =
                appModule->ioSystem()->exportApplicationItems()
                .targetFile(filepathFrom(strFilepath))
                .targetFormat(format)
                .withItems(this->guiApp()->selectionModel()->selectedItems())
                .withParameters(appModule->findWriterParameters(format))
                .withMessenger(appModule)
                .withTaskProgress(progress)
                .execute();
        if (okExport)
            appModule->emitInfo(fmt::format(Command::textIdTr("Export time: {}ms"), chrono.elapsed()));
    });
    this->taskMgr()->setTitle(taskId, to_stdString(QFileInfo(strFilepath).fileName()));
    this->taskMgr()->run(taskId);
    ImportExportSettings::save(lastSettings);
}

bool CommandExportSelectedApplicationItems::getEnabledStatus() const
{
    return this->app()->documentCount() != 0;
}

CommandCloseCurrentDocument::CommandCloseCurrentDocument(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("Close \"%1\""));
    action->setToolTip(action->text());
    action->setIcon(mayoTheme()->icon(Theme::Icon::Cross));
    action->setShortcut(Qt::CTRL + Qt::Key_W);
    this->setAction(action);

    QObject::connect(
                context, &IAppContext::currentDocumentChanged,
                this, &CommandCloseCurrentDocument::updateActionText
    );
    this->app()->signalDocumentNameChanged.connectSlot([=](const DocumentPtr& doc) {
        if (this->currentDocument() == doc->identifier())
            this->updateActionText(this->currentDocument());
    });

    this->updateActionText(-1);
}

void CommandCloseCurrentDocument::execute()
{
    closeDocument(this->context(), this->currentDocument());
}

bool CommandCloseCurrentDocument::getEnabledStatus() const
{
    return this->app()->documentCount() != 0;
}

void CommandCloseCurrentDocument::updateActionText(Document::Identifier docId)
{
    DocumentPtr docPtr = this->app()->findDocumentByIdentifier(docId);
    const QString docName = to_QString(docPtr ? docPtr->name() : std::string{});
    const QString textActionClose =
            docPtr ?
                tr("Close \"%1\"").arg(strFilepathQuoted(docName)) :
                tr("Close");
    this->action()->setText(textActionClose);
}

CommandCloseAllDocuments::CommandCloseAllDocuments(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("Close all"));
    action->setToolTip(Command::tr("Close all documents"));
    action->setShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_W);
    this->setAction(action);
}

void CommandCloseAllDocuments::execute()
{
    while (!this->guiApp()->guiDocuments().empty())
        closeDocument(this->context(), this->currentDocument());
}

bool CommandCloseAllDocuments::getEnabledStatus() const
{
    return this->app()->documentCount() != 0;
}

CommandCloseAllDocumentsExceptCurrent::CommandCloseAllDocumentsExceptCurrent(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("Close all except current"));
    action->setToolTip(Command::tr("Close all except current document"));
    this->setAction(action);

    QObject::connect(
                context, &IAppContext::currentDocumentChanged,
                this, &CommandCloseAllDocumentsExceptCurrent::updateActionText
    );
    this->app()->signalDocumentNameChanged.connectSlot([=](const DocumentPtr& doc) {
        if (this->currentDocument() == doc->identifier())
            this->updateActionText(this->currentDocument());
    });

    this->updateActionText(-1);
}

void CommandCloseAllDocumentsExceptCurrent::execute()
{
#if 4
    GuiDocument* currentGuiDoc = this->currentGuiDocument();
    std::vector<GuiDocument*> vecGuiDoc;
    for (GuiDocument* guiDoc : this->guiApp()->guiDocuments())
        vecGuiDoc.push_back(guiDoc);

    for (GuiDocument* guiDoc : vecGuiDoc) {
        if (guiDoc != currentGuiDoc)
            closeDocument(this->context(), guiDoc->document()->identifier());
    }
#endif
}

bool CommandCloseAllDocumentsExceptCurrent::getEnabledStatus() const
{
    return this->app()->documentCount() != 0;
}

void CommandCloseAllDocumentsExceptCurrent::updateActionText(Document::Identifier docId)
{
    DocumentPtr docPtr = this->app()->findDocumentByIdentifier(docId);
    const QString docName = to_QString(docPtr ? docPtr->name() : std::string{});
    const QString textActionClose =
            docPtr ?
                Command::tr("Close all except \"%1\"").arg(strFilepathQuoted(docName)) :
                Command::tr("Close all except current");
    this->action()->setText(textActionClose);
}

CommandMainWidgetToggleFullscreen::CommandMainWidgetToggleFullscreen(IAppContext* context)
    : Command(context)
{
    auto action = new QAction(this);
    action->setText(Command::tr("Fullscreen"));
    action->setToolTip(Command::tr("Switch Fullscreen/Normal"));
    action->setShortcut(Qt::Key_F11);
    action->setCheckable(true);
    action->setChecked(context->mainWidget()->isFullScreen());
    this->setAction(action);
}

void CommandMainWidgetToggleFullscreen::execute()
{
    auto widget = this->mainWidget();
    if (widget->isFullScreen()) {
        if (m_previousWindowState.testFlag(Qt::WindowMaximized))
            widget->showMaximized();
        else
            widget->showNormal();
    }
    else {
        m_previousWindowState = widget->windowState();
        widget->showFullScreen();
    }
}

} // namespace Mayo

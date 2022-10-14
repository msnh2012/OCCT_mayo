/****************************************************************************
** Copyright (c) 2021, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "../base/application.h"
#include "../base/application_item_selection_model.h"
#include "../base/cpp_utils.h"
#include "../base/document.h"
#include "../base/global.h"
#include "../base/io_format.h"
#include "../base/io_system.h"
#include "../base/messenger.h"
#include "../base/settings.h"
#include "../base/task_manager.h"
#include "../graphics/graphics_object_driver.h"
#include "../graphics/graphics_utils.h"
#include "../gui/gui_application.h"
#include "../gui/gui_document.h"
#include "app_module.h"
#include "commands_file.h"
#include "commands_tools.h"
#include "commands_window.h"
#include "dialog_about.h"
#include "dialog_task_manager.h"
#include "document_property_group.h"
#include "document_tree_node_properties_providers.h"
#include "filepath_conv.h"
#include "gui_document_list_model.h"
#include "item_view_buttons.h"
#include "qstring_conv.h"
#include "qtgui_utils.h"
#include "qtwidgets_utils.h"
#include "theme.h"
#include "widget_file_system.h"
#include "widget_gui_document.h"
#include "widget_message_indicator.h"
#include "widget_model_tree.h"
#include "widget_occ_view.h"
#include "widget_occ_view_controller.h"
#include "widget_properties_editor.h"

#ifdef Q_OS_WIN
#  include "windows/win_taskbar_global_progress.h"
#endif

#include <QtCore/QElapsedTimer>
#include <QtCore/QMimeData>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtGui/QDesktopServices>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QActionGroup> // WARNING Qt5 <QtWidgets/...> / Qt6 <QtGui/...>
#include <QtWidgets/QApplication>
#include <QtDebug>

namespace Mayo {

namespace Internal {

static void handleMessage(Messenger::MessageType msgType, const QString& text, QWidget* mainWnd)
{
    switch (msgType) {
    case Messenger::MessageType::Trace:
        break;
    case Messenger::MessageType::Info:
        WidgetMessageIndicator::showInfo(text, mainWnd);
        break;
    case Messenger::MessageType::Warning:
        QtWidgetsUtils::asyncMsgBoxWarning(mainWnd, MainWindow::tr("Warning"), text);
        break;
    case Messenger::MessageType::Error:
        QtWidgetsUtils::asyncMsgBoxCritical(mainWnd, MainWindow::tr("Error"), text);
        break;
    }
}

} // namespace Internal

class AppContext : public IAppContext {
public:
    AppContext(MainWindow* wnd)
        : m_wnd(wnd)
    {
        QObject::connect(
                    m_wnd->m_ui->combo_GuiDocuments, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, &AppContext::onCurrentDocumentIndexChanged
        );
    }

    GuiApplication* guiApp() const override {
        return m_wnd->m_guiApp;
    }

    TaskManager* taskMgr() const override {
        return &m_wnd->m_taskMgr;
    }

    QWidget* widgetMain() const override {
        return m_wnd;
    }

    QWidget* widgetLeftSidebar() const override {
        return m_wnd->m_ui->widget_Left;
    }

    ModeWidgetMain modeWidgetMain() const override
    {
        auto widget = m_wnd->m_ui->stack_Main->currentWidget();
        if (widget == m_wnd->m_ui->page_MainHome)
            return ModeWidgetMain::Home;
        else if (widget == m_wnd->m_ui->page_MainControl)
            return ModeWidgetMain::Documents;

        return ModeWidgetMain::Unknown;
    }

    int findDocumentIndex(Document::Identifier docId) const override
    {
        int index = -1;
        auto widgetDoc = this->findWidgetGuiDocument([&](WidgetGuiDocument* candidate) {
                ++index;
                return candidate->documentIdentifier() == docId;
        });
        return widgetDoc ? index : -1;
    }

    Document::Identifier findDocumentFromIndex(int index) const override
    {
        auto widgetDoc = m_wnd->widgetGuiDocument(index);
        return widgetDoc ? widgetDoc->documentIdentifier() : -1;
    }

    Document::Identifier currentDocument() const override
    {
        const int index = m_wnd->m_ui->combo_GuiDocuments->currentIndex();
        auto widgetDoc = m_wnd->widgetGuiDocument(index);
        return widgetDoc ? widgetDoc->documentIdentifier() : -1;
    }

    void setCurrentDocument(Document::Identifier docId) override
    {
        auto widgetDoc = this->findWidgetGuiDocument([=](WidgetGuiDocument* widgetDoc) {
            return widgetDoc->documentIdentifier() == docId;
        });
        const int docIndex = m_wnd->m_ui->stack_GuiDocuments->indexOf(widgetDoc);
        m_wnd->m_ui->combo_GuiDocuments->setCurrentIndex(docIndex);
    }

    void updateControlsEnabledStatus() override {
        m_wnd->updateControlsActivation();
    }

    void deleteDocumentWidget(const DocumentPtr& doc) override
    {
        QWidget* widgetDoc = this->findWidgetGuiDocument([&](WidgetGuiDocument* widgetDoc) {
            return widgetDoc->documentIdentifier() == doc->identifier();
        });
        if (widgetDoc) {
            m_wnd->m_ui->stack_GuiDocuments->removeWidget(widgetDoc);
            widgetDoc->deleteLater();
        }
    }

private:
    WidgetGuiDocument* findWidgetGuiDocument(std::function<bool(WidgetGuiDocument*)> fn) const
    {
        const int widgetCount = m_wnd->m_ui->stack_GuiDocuments->count();
        for (int i = 0; i < widgetCount; ++i) {
            auto candidate = m_wnd->widgetGuiDocument(i);
            if (candidate && fn(candidate))
                return candidate;
        }

        return nullptr;
    }

    void onCurrentDocumentIndexChanged(int docIndex)
    {
        auto widgetDoc = m_wnd->widgetGuiDocument(docIndex);
        emit this->currentDocumentChanged(widgetDoc ? widgetDoc->documentIdentifier() : -1);
    }

    MainWindow* m_wnd = nullptr;
};

MainWindow::MainWindow(GuiApplication* guiApp, QWidget *parent)
    : QMainWindow(parent),
      m_guiApp(guiApp),
      m_ui(new Ui_MainWindow)
{
    m_ui->setupUi(this);
    m_ui->widget_ModelTree->registerGuiApplication(guiApp);

    m_ui->splitter_Main->setChildrenCollapsible(false);
    m_ui->splitter_Main->setStretchFactor(0, 1);
    m_ui->splitter_Main->setStretchFactor(1, 3);

    m_ui->splitter_ModelTree->setStretchFactor(0, 1);
    m_ui->splitter_ModelTree->setStretchFactor(1, 2);

    m_ui->stack_LeftContents->setCurrentIndex(0);

    m_ui->widget_Properties->setRowHeightFactor(1.4);
    m_ui->widget_Properties->clear();

    m_appContext = new AppContext(this);
    auto fnGetAction = [=](std::string_view name) {
        return this->getCommand(name)->action();
    };
    // "File" commands
    this->addCommand<CommandNewDocument>("new-doc");
    this->addCommand<CommandOpenDocuments>("open-docs");
    this->addCommand<CommandRecentFiles>("recent-files", m_ui->menu_File);
    this->addCommand<CommandImportInCurrentDocument>("import");
    this->addCommand<CommandExportSelectedApplicationItems>("export");
    this->addCommand<CommandCloseCurrentDocument>("close-doc");
    this->addCommand<CommandCloseAllDocuments>("close-all-docs");
    this->addCommand<CommandCloseAllDocumentsExceptCurrent>("close-all-docs-except-current");
    this->addCommand<CommandQuitApplication>("quit");
    // "Tools" commands
    this->addCommand<CommandSaveViewImage>("save-view-image");
    this->addCommand<CommandInspectXde>("inspect-xde");
    this->addCommand<CommandEditOptions>("edit-options");
    // "Window" commands
    this->addCommand<CommandMainWidgetToggleFullscreen>("fullscreen");
    this->addCommand<CommandLeftSidebarWidgetToggle>("toggle-left-sidebar");
    this->addCommand<CommandPreviousDocument>("previous-doc");
    this->addCommand<CommandNextDocument>("next-doc");

    {
        auto menu = m_ui->menu_File;
        menu->addAction(fnGetAction("new-doc"));
        menu->addAction(fnGetAction("open-docs"));
        menu->addAction(fnGetAction("recent-files"));
        menu->addSeparator();
        menu->addAction(fnGetAction("import"));
        menu->addAction(fnGetAction("export"));
        menu->addSeparator();
        menu->addAction(fnGetAction("close-doc"));
        menu->addAction(fnGetAction("close-all-docs-except-current"));
        menu->addAction(fnGetAction("close-all-docs"));
        menu->addSeparator();
        menu->addAction(fnGetAction("quit"));
    }

    {
        auto menu = m_ui->menu_Tools;
        menu->addAction(fnGetAction("save-view-image"));
        menu->addAction(fnGetAction("inspect-xde"));
        menu->addSeparator();
        menu->addAction(fnGetAction("edit-options"));
    }

    {
        auto menu = m_ui->menu_Window;
        menu->addAction(fnGetAction("toggle-left-sidebar"));
        menu->addAction(fnGetAction("fullscreen"));
        menu->addSeparator();
        menu->addAction(fnGetAction("previous-doc"));
        menu->addAction(fnGetAction("next-doc"));
    }

    m_ui->btn_PreviousGuiDocument->setDefaultAction(fnGetAction("previous-doc"));
    m_ui->btn_NextGuiDocument->setDefaultAction(fnGetAction("next-doc"));
    m_ui->btn_CloseGuiDocument->setDefaultAction(fnGetAction("close-doc"));

    m_ui->actionAboutMayo->setText(tr("About %1").arg(QApplication::applicationName()));
    m_ui->actionZoomIn->setIcon(mayoTheme()->icon(Theme::Icon::ZoomIn));
    m_ui->actionZoomOut->setIcon(mayoTheme()->icon(Theme::Icon::ZoomOut));
    m_ui->btn_CloseLeftSideBar->setIcon(mayoTheme()->icon(Theme::Icon::BackSquare));

    m_ui->actionToggleOriginTrihedron->setChecked(false);
    m_ui->actionTogglePerformanceStats->setChecked(false);

    mayoTheme()->setupHeaderComboBox(m_ui->combo_LeftContents);
    mayoTheme()->setupHeaderComboBox(m_ui->combo_GuiDocuments);

    auto sigComboIndexChanged = qOverload<int>(&QComboBox::currentIndexChanged);
    // "HomeFiles" actions
    QObject::connect(
                m_ui->widget_HomeFiles, &WidgetHomeFiles::newDocumentRequested,
                this->getCommand("new-doc"), &Command::execute
    );
    QObject::connect(
                m_ui->widget_HomeFiles, &WidgetHomeFiles::openDocumentsRequested,
                this->getCommand("open-docs"), &Command::execute
    );
    QObject::connect(
                m_ui->widget_HomeFiles, &WidgetHomeFiles::recentFileOpenRequested,
                this, &MainWindow::openDocument
    );
    // "Display" actions
    QObject::connect(
                m_ui->menu_Display, &QMenu::aboutToShow,
                this, &MainWindow::createMenuDisplayMode
    );
    {
        auto group = new QActionGroup(m_ui->menu_Projection);
        group->setExclusive(true);
        group->addAction(m_ui->actionProjectionOrthographic);
        group->addAction(m_ui->actionProjectionPerspective);
    }
    QObject::connect(m_ui->menu_Projection, &QMenu::triggered, this, [=](QAction* action) {
        if (this->currentWidgetGuiDocument()) {
            const GuiDocument* guiDoc = this->currentWidgetGuiDocument()->guiDocument();
            guiDoc->v3dView()->Camera()->SetProjectionType(
                        action == m_ui->actionProjectionOrthographic ?
                            Graphic3d_Camera::Projection_Orthographic :
                            Graphic3d_Camera::Projection_Perspective);
            guiDoc->v3dView()->Update();
        }
    });
    QObject::connect(
                m_ui->actionToggleOriginTrihedron, &QAction::toggled,
                this, &MainWindow::toggleCurrentDocOriginTrihedron);
    QObject::connect(
                m_ui->actionTogglePerformanceStats, &QAction::toggled,
                this, &MainWindow::toggleCurrentDocPerformanceStats);
    QObject::connect(
                m_ui->actionZoomIn, &QAction::triggered,
                this, &MainWindow::zoomInCurrentDoc);
    QObject::connect(
                m_ui->actionZoomOut, &QAction::triggered,
                this, &MainWindow::zoomOutCurrentDoc);
    // "Help" actions
    QObject::connect(
                m_ui->actionReportBug, &QAction::triggered,
                this, &MainWindow::reportbug);
    QObject::connect(
                m_ui->actionAboutMayo, &QAction::triggered,
                this, &MainWindow::aboutMayo);
    // "Window" actions and navigation in documents
    QObject::connect(
                m_ui->combo_GuiDocuments, sigComboIndexChanged,
                this, &MainWindow::onCurrentDocumentIndexChanged);
    QObject::connect(
                m_ui->widget_FileSystem, &WidgetFileSystem::locationActivated,
                this, &MainWindow::onWidgetFileSystemLocationActivated);
    // Left header bar of controls
    QObject::connect(
                m_ui->btn_CloseLeftSideBar, &QAbstractButton::clicked,
                this->getCommand("toggle-left-sidebar"), &Command::execute);
    // ...
    QObject::connect(
                m_ui->combo_LeftContents, sigComboIndexChanged,
                this, &MainWindow::onLeftContentsPageChanged);
    QObject::connect(
                m_ui->listView_OpenedDocuments, &QListView::clicked,
                this, [=](const QModelIndex& index) { this->setCurrentDocumentIndex(index.row()); });
    QObject::connect(
                AppModule::get(), &AppModule::message,
                this, [=](Messenger::MessageType msgType, const QString& text) {
        Internal::handleMessage(msgType, text, this);
    });
    guiApp->signalGuiDocumentAdded.connectSlot(&MainWindow::onGuiDocumentAdded, this);
    guiApp->selectionModel()->signalChanged.connectSlot(&MainWindow::onApplicationItemSelectionChanged, this);
    // Creation of annex objects
    {
        // Opened documents GUI
        auto listViewBtns = new ItemViewButtons(m_ui->listView_OpenedDocuments, this);
        auto actionCloseDoc = fnGetAction("close-doc");
        listViewBtns->addButton(1, actionCloseDoc->icon(), actionCloseDoc->toolTip());
        listViewBtns->setButtonDetection(1, -1, QVariant());
        listViewBtns->setButtonDisplayColumn(1, 0);
        listViewBtns->setButtonDisplayModes(1, ItemViewButtons::DisplayOnDetection);
        listViewBtns->setButtonItemSide(1, ItemViewButtons::ItemRightSide);
        const int iconSize = this->style()->pixelMetric(QStyle::PM_ListViewIconSize);
        listViewBtns->setButtonIconSize(1, QSize(iconSize * 0.66, iconSize * 0.66));
        listViewBtns->installDefaultItemDelegate();
        QObject::connect(listViewBtns, &ItemViewButtons::buttonClicked, this, [=](int btnId, QModelIndex index) {
            if (btnId == 1) {
                auto widgetDoc = this->widgetGuiDocument(index.row());
                if (widgetDoc)
                    FileCommandTools::closeDocument(m_appContext, widgetDoc->documentIdentifier());
            }
        });
    }

    new DialogTaskManager(&m_taskMgr, this);

    // BEWARE MainWindow::onGuiDocumentAdded() must be called before
    // MainWindow::onCurrentDocumentIndexChanged()
    auto guiDocModel = new GuiDocumentListModel(guiApp, this);
    m_ui->combo_GuiDocuments->setModel(guiDocModel);
    m_ui->listView_OpenedDocuments->setModel(guiDocModel);

    // Finalize setup
    this->setAcceptDrops(true);
    m_ui->widget_LeftHeader->installEventFilter(this);
    m_ui->widget_ControlGuiDocuments->installEventFilter(this);
    m_ui->stack_GuiDocuments->installEventFilter(this);
    this->onLeftContentsPageChanged(m_ui->stack_LeftContents->currentIndex());
    this->updateControlsActivation();
    m_ui->widget_MouseCoords->hide();

    this->onCurrentDocumentIndexChanged(-1);
}

MainWindow::~MainWindow()
{
    delete m_ui;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    auto fnSizeBtn = [](const QWidget* container, const QWidget* widgetHeightRef) {
        const int btnSideLen = widgetHeightRef->frameGeometry().height();
        const QList<QAbstractButton*> listBtn = container->findChildren<QAbstractButton*>();
        for (QAbstractButton* btn : listBtn)
            btn->setFixedSize(btnSideLen, btnSideLen);
    };
    const QEvent::Type eventType = event->type();
    if (watched == m_ui->widget_ControlGuiDocuments && eventType == QEvent::Show) {
        fnSizeBtn(m_ui->widget_ControlGuiDocuments, m_ui->combo_GuiDocuments);
        return true;
    }

    if (watched == m_ui->widget_LeftHeader && eventType == QEvent::Show) {
        fnSizeBtn(m_ui->widget_LeftHeader, m_ui->combo_LeftContents);
        return true;
    }

    if (watched == m_ui->stack_GuiDocuments) {
        if (eventType == QEvent::Enter || eventType == QEvent::Leave) {
            m_ui->widget_MouseCoords->setHidden(eventType == QEvent::Leave);
            return true;
        }
    }

    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> listUrl = event->mimeData()->urls();
    std::vector<FilePath> listFilePath;
    for (const QUrl& url : listUrl) {
        if (url.isLocalFile())
            listFilePath.push_back(filepathFrom(url.toLocalFile()));
    }

    event->acceptProposedAction();
    FileCommandTools::openDocumentsFromList(m_appContext, listFilePath);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
#if defined(Q_OS_WIN) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    constexpr Qt::FindChildOption findMode = Qt::FindDirectChildrenOnly;
    auto winProgress = this->findChild<WinTaskbarGlobalProgress*>(QString(), findMode);
    if (!winProgress)
        winProgress = new WinTaskbarGlobalProgress(&m_taskMgr, this);

    winProgress->setWindow(this->windowHandle());
#endif
}

void MainWindow::toggleCurrentDocOriginTrihedron()
{
    WidgetGuiDocument* widget = this->currentWidgetGuiDocument();
    if (widget) {
        widget->guiDocument()->toggleOriginTrihedronVisibility();
        widget->guiDocument()->graphicsScene()->redraw();
    }
}

void MainWindow::toggleCurrentDocPerformanceStats()
{
    WidgetGuiDocument* widget = this->currentWidgetGuiDocument();
    GuiDocument* guiDoc = widget ? widget->guiDocument() : nullptr;
    if (guiDoc) {
        CppUtils::toggle(guiDoc->v3dView()->ChangeRenderingParams().ToShowStats);
        guiDoc->graphicsScene()->redraw();
    }
}

void MainWindow::zoomInCurrentDoc()
{
    this->currentWidgetGuiDocument()->controller()->zoomIn();
}

void MainWindow::zoomOutCurrentDoc()
{
    this->currentWidgetGuiDocument()->controller()->zoomOut();
}

void MainWindow::aboutMayo()
{
    auto dlg = new DialogAbout(this);
    QtWidgetsUtils::asyncDialogExec(dlg);
}

void MainWindow::reportbug()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/fougue/mayo/issues")));
}

void MainWindow::onApplicationItemSelectionChanged()
{
    WidgetModelTree* uiModelTree = m_ui->widget_ModelTree;
    WidgetPropertiesEditor* uiProps = m_ui->widget_Properties;

    uiProps->clear();
    Span<const ApplicationItem> spanAppItem = m_guiApp->selectionModel()->selectedItems();
    if (spanAppItem.size() == 1) {
        const ApplicationItem& appItem = spanAppItem.front();
        if (appItem.isDocument()) {
            auto dataProps = new DocumentPropertyGroup(appItem.document());
            uiProps->editProperties(dataProps, uiProps->addGroup(tr("Data")));
            m_ptrCurrentNodeDataProperties.reset(dataProps);
        }
        else if (appItem.isDocumentTreeNode()) {
            const DocumentTreeNode& docTreeNode = appItem.documentTreeNode();
            auto dataProps = AppModule::get()->properties(docTreeNode);
            if (dataProps) {
                uiProps->editProperties(dataProps.get(), uiProps->addGroup(tr("Data")));
                dataProps->signalPropertyChanged.connectSlot([=]{ uiModelTree->refreshItemText(appItem); });
                m_ptrCurrentNodeDataProperties = std::move(dataProps);
            }

            GuiDocument* guiDoc = m_guiApp->findGuiDocument(appItem.document());
            std::vector<GraphicsObjectPtr> vecGfxObject;
            guiDoc->foreachGraphicsObject(docTreeNode.id(), [&](GraphicsObjectPtr gfxObject) {
                vecGfxObject.push_back(std::move(gfxObject));
            });
            auto commonGfxDriver = GraphicsObjectDriver::getCommon(vecGfxObject);
            if (commonGfxDriver) {
                auto gfxProps = commonGfxDriver->properties(vecGfxObject);
                if (gfxProps) {
                    uiProps->editProperties(gfxProps.get(), uiProps->addGroup(tr("Graphics")));
                    gfxProps->signalPropertyChanged.connectSlot([=]{ guiDoc->graphicsScene()->redraw(); });
                    m_ptrCurrentNodeGraphicsProperties = std::move(gfxProps);
                }
            }
        }

        auto app = m_guiApp->application();
        if (AppModule::get()->properties()->linkWithDocumentSelector) {
            const int index = app->findIndexOfDocument(appItem.document());
            if (index != -1)
                this->setCurrentDocumentIndex(index);
        }
    }
    else {
        // TODO
        uiProps->clear();
    }

    this->updateControlsActivation();
}

void MainWindow::onOperationFinished(bool ok, const QString &msg)
{
    if (ok)
        WidgetMessageIndicator::showInfo(msg, this);
    else
        QtWidgetsUtils::asyncMsgBoxCritical(this, tr("Error"), msg);
}

void MainWindow::onGuiDocumentAdded(GuiDocument* guiDoc)
{
    auto gfxScene = guiDoc->graphicsScene();
    // Configure highlighting aspect
    auto fnConfigureHighlightStyle = [=](Prs3d_Drawer* drawer) {
        const QColor fillAreaQColor = mayoTheme()->color(Theme::Color::Graphic3d_AspectFillArea);
        if (!fillAreaQColor.isValid())
            return;

        auto fillArea = new Graphic3d_AspectFillArea3d;
        auto defaultShadingAspect = gfxScene->drawerDefault()->ShadingAspect();
        if (defaultShadingAspect && defaultShadingAspect->Aspect())
            *fillArea = *defaultShadingAspect->Aspect();

        const Quantity_Color fillAreaColor = QtGuiUtils::toPreferredColorSpace(fillAreaQColor);
        fillArea->SetInteriorColor(fillAreaColor);
        Graphic3d_MaterialAspect fillMaterial(Graphic3d_NOM_PLASTER);
        fillMaterial.SetColor(fillAreaColor);
        //fillMaterial.SetTransparency(0.1f);
        fillArea->SetFrontMaterial(fillMaterial);
        fillArea->SetBackMaterial(fillMaterial);
        drawer->SetDisplayMode(AIS_Shaded);
        drawer->SetBasicFillAreaAspect(fillArea);
    };
    fnConfigureHighlightStyle(gfxScene->drawerHighlight(Prs3d_TypeOfHighlight_LocalSelected).get());
    fnConfigureHighlightStyle(gfxScene->drawerHighlight(Prs3d_TypeOfHighlight_Selected).get());

    // Configure 3D view behavior with respect to application settings
    auto appModule = AppModule::get();
    auto appProps = appModule->properties();
    auto widget = new WidgetGuiDocument(guiDoc);
    auto widgetCtrl = widget->controller();
    widgetCtrl->setInstantZoomFactor(appProps->instantZoomFactor);
    widgetCtrl->setNavigationStyle(appProps->navigationStyle);
    if (appProps->defaultShowOriginTrihedron) {
        guiDoc->toggleOriginTrihedronVisibility();
        gfxScene->redraw();
    }

    appModule->settings()->signalChanged.connectSlot([=](Property* setting) {
        if (setting == &appProps->instantZoomFactor)
            widgetCtrl->setInstantZoomFactor(appProps->instantZoomFactor);
        else if (setting == &appProps->navigationStyle)
            widgetCtrl->setNavigationStyle(appProps->navigationStyle);
    });

    // React to mouse move in 3D view:
    //   * update highlighting
    //   * compute and display 3D mouse coordinates(by silent picking)
    widgetCtrl->signalMouseMoved.connectSlot([=](int xPos, int yPos) {
        gfxScene->highlightAt(xPos, yPos, guiDoc->v3dView());
        widget->view()->redraw();
        auto selector = gfxScene->mainSelector();
        selector->Pick(xPos, yPos, guiDoc->v3dView());
        const gp_Pnt pos3d =
                selector->NbPicked() > 0 ?
                    selector->PickedPoint(1) :
                    GraphicsUtils::V3dView_to3dPosition(guiDoc->v3dView(), xPos, yPos);
        m_ui->label_ValuePosX->setText(QString::number(pos3d.X(), 'f', 3));
        m_ui->label_ValuePosY->setText(QString::number(pos3d.Y(), 'f', 3));
        m_ui->label_ValuePosZ->setText(QString::number(pos3d.Z(), 'f', 3));
    });

    m_ui->stack_GuiDocuments->addWidget(widget);
    this->updateControlsActivation();
    const int newDocIndex = m_guiApp->application()->documentCount() - 1;
    QTimer::singleShot(0, this, [=]{ this->setCurrentDocumentIndex(newDocIndex); });
}

void MainWindow::onWidgetFileSystemLocationActivated(const QFileInfo& loc)
{
    this->openDocument(filepathFrom(loc));
}

void MainWindow::onLeftContentsPageChanged(int pageId)
{
    m_ui->stack_LeftContents->setCurrentIndex(pageId);
    QWidget* placeHolder = this->recreateLeftHeaderPlaceHolder();
    if (m_ui->stack_LeftContents->currentWidget() == m_ui->page_ModelTree && placeHolder) {
        const int btnSideLen = m_ui->combo_LeftContents->frameGeometry().height();
        auto btnSettings = new QToolButton(placeHolder);
        btnSettings->setAutoRaise(true);
        btnSettings->setFixedSize(btnSideLen, btnSideLen);
        btnSettings->setIcon(mayoTheme()->icon(Theme::Icon::Gear));
        btnSettings->setToolTip(tr("Options"));
        placeHolder->layout()->addWidget(btnSettings);
        btnSettings->setMenu(this->createMenuModelTreeSettings());
        btnSettings->setPopupMode(QToolButton::InstantPopup);
    }
    else {
        delete placeHolder;
    }
}

void MainWindow::onCurrentDocumentIndexChanged(int idx)
{
    m_ui->stack_GuiDocuments->setCurrentIndex(idx);
    QAbstractItemView* view = m_ui->listView_OpenedDocuments;
    view->setCurrentIndex(view->model()->index(idx, 0));

    this->updateControlsActivation();

    const DocumentPtr docPtr = m_guiApp->application()->findDocumentByIndex(idx);
    const FilePath docFilePath = docPtr ? docPtr->filePath() : FilePath();
    m_ui->widget_FileSystem->setLocation(filepathTo<QFileInfo>(docFilePath));

    if (this->currentWidgetGuiDocument()) {
        const GuiDocument* guiDoc = this->currentWidgetGuiDocument()->guiDocument();
        // Sync action with current visibility status of origin trihedron
        {
            QSignalBlocker sigBlk(m_ui->actionToggleOriginTrihedron); Q_UNUSED(sigBlk);
            m_ui->actionToggleOriginTrihedron->setChecked(guiDoc->isOriginTrihedronVisible());
        }
        // Sync action with current visibility status of rendering performance stats
        {
            QSignalBlocker sigBlk(m_ui->actionTogglePerformanceStats); Q_UNUSED(sigBlk);
            m_ui->actionTogglePerformanceStats->setChecked(guiDoc->v3dView()->ChangeRenderingParams().ToShowStats);
        }
        // Sync menu with current projection type
        {
            const Graphic3d_Camera::Projection viewProjectionType =
                    guiDoc->v3dView()->Camera()->ProjectionType();
            Q_ASSERT(viewProjectionType == Graphic3d_Camera::Projection_Perspective
                     || viewProjectionType == Graphic3d_Camera::Projection_Orthographic);
            QAction* actionProjection =
                viewProjectionType == Graphic3d_Camera::Projection_Perspective ?
                        m_ui->actionProjectionPerspective :
                        m_ui->actionProjectionOrthographic;
            [[maybe_unused]] QSignalBlocker sigBlk(m_ui->menu_Projection);
            actionProjection->setChecked(true);
        }
    }
    else {
        m_ui->actionToggleOriginTrihedron->setChecked(false);
        m_ui->actionTogglePerformanceStats->setChecked(false);
    }
 }

void MainWindow::openDocument(const FilePath& fp)
{
    FileCommandTools::openDocument(m_appContext, fp);
}

void MainWindow::openDocumentsFromList(Span<const FilePath> listFilePath)
{
    FileCommandTools::openDocumentsFromList(m_appContext, listFilePath);
}

void MainWindow::updateControlsActivation()
{
    const QWidget* currMainPage = m_ui->stack_Main->currentWidget();
    const int appDocumentsCount = m_guiApp->application()->documentCount();
    const bool appDocumentsEmpty = appDocumentsCount == 0;
    QWidget* newMainPage = appDocumentsEmpty ? m_ui->page_MainHome : m_ui->page_MainControl;
    if (currMainPage != newMainPage)
        m_ui->stack_Main->setCurrentWidget(newMainPage);

    for (auto [name, cmd] : m_mapCommand) {
        cmd->action()->setEnabled(cmd->getEnabledStatus());
    }

    m_ui->menu_Projection->setEnabled(!appDocumentsEmpty);
    m_ui->actionProjectionOrthographic->setEnabled(!appDocumentsEmpty);
    m_ui->actionProjectionPerspective->setEnabled(!appDocumentsEmpty);
    m_ui->actionDisplayMode->setEnabled(!appDocumentsEmpty);
    m_ui->actionToggleOriginTrihedron->setEnabled(!appDocumentsEmpty);
    m_ui->actionTogglePerformanceStats->setEnabled(!appDocumentsEmpty);
    m_ui->actionZoomIn->setEnabled(!appDocumentsEmpty);
    m_ui->actionZoomOut->setEnabled(!appDocumentsEmpty);
    m_ui->combo_GuiDocuments->setEnabled(!appDocumentsEmpty);
}

int MainWindow::currentDocumentIndex() const
{
    return m_ui->combo_GuiDocuments->currentIndex();
}

void MainWindow::setCurrentDocumentIndex(int idx)
{
    m_ui->combo_GuiDocuments->setCurrentIndex(idx);
}

WidgetGuiDocument* MainWindow::widgetGuiDocument(int idx) const
{
    return qobject_cast<WidgetGuiDocument*>(m_ui->stack_GuiDocuments->widget(idx));
}

WidgetGuiDocument* MainWindow::currentWidgetGuiDocument() const
{
    return this->widgetGuiDocument(this->currentDocumentIndex());
}

QWidget* MainWindow::findLeftHeaderPlaceHolder() const
{
    return m_ui->widget_LeftHeader->findChild<QWidget*>(
                "LeftHeaderPlaceHolder", Qt::FindDirectChildrenOnly);
}

QWidget* MainWindow::recreateLeftHeaderPlaceHolder()
{
    QWidget* placeHolder = this->findLeftHeaderPlaceHolder();
    delete placeHolder;
    placeHolder = new QWidget(m_ui->widget_LeftHeader);
    placeHolder->setObjectName("LeftHeaderPlaceHolder");
    auto layoutPlaceHolder = new QHBoxLayout(placeHolder);
    layoutPlaceHolder->setContentsMargins(0, 0, 0, 0);
    layoutPlaceHolder->setSpacing(0);
    m_ui->Layout_WidgetLeftHeader->insertWidget(2, placeHolder);
    return placeHolder;
}

QMenu* MainWindow::createMenuModelTreeSettings()
{
    auto menu = new QMenu(this->findLeftHeaderPlaceHolder());
    menu->setToolTipsVisible(true);

    // Link with document selector
    auto appModule = AppModule::get();
    QAction* action = menu->addAction(to_QString(appModule->properties()->linkWithDocumentSelector.name().tr()));
    action->setCheckable(true);
    QObject::connect(action, &QAction::triggered, this, [=](bool on) {
        appModule->properties()->linkWithDocumentSelector.setValue(on);
    });

    // Model tree user actions
    menu->addSeparator();
    const WidgetModelTree_UserActions userActions = m_ui->widget_ModelTree->createUserActions(menu);
    for (QAction* action : userActions.items)
        menu->addAction(action);

    // Sync before menu show
    QObject::connect(menu, &QMenu::aboutToShow, this, [=]{
        action->setChecked(appModule->properties()->linkWithDocumentSelector);
        if (userActions.fnSyncItems)
            userActions.fnSyncItems();
    });

    return menu;
}

QMenu* MainWindow::createMenuDisplayMode()
{
    QMenu* menu = m_ui->actionDisplayMode->menu();
    if (!menu) {
        menu = new QMenu(this);
        m_ui->actionDisplayMode->setMenu(menu);
    }

    menu->clear();

    WidgetGuiDocument* widgetGuiDoc = this->currentWidgetGuiDocument();
    GuiDocument* guiDoc = widgetGuiDoc ? widgetGuiDoc->guiDocument() : nullptr;
    if (!guiDoc)
        return menu;

    const auto spanDrivers = m_guiApp->graphicsObjectDrivers();
    for (const GraphicsObjectDriverPtr& driver : spanDrivers) {
        if (driver->displayModes().empty())
            continue; // Skip

        if (driver != spanDrivers.front())
            menu->addSeparator();

        auto group = new QActionGroup(menu);
        group->setExclusive(true);
        for (const Enumeration::Item& displayMode : driver->displayModes().items()) {
            auto action = new QAction(to_QString(displayMode.name.tr()), menu);
            action->setCheckable(true);
            action->setData(displayMode.value);
            menu->addAction(action);
            group->addAction(action);
            if (displayMode.value == guiDoc->activeDisplayMode(driver))
                action->setChecked(true);
        }

        QObject::connect(group, &QActionGroup::triggered, this, [=](QAction* action) {
            guiDoc->setActiveDisplayMode(driver, action->data().toInt());
            guiDoc->graphicsScene()->redraw();
        });
    }

    return menu;
}

Command* MainWindow::getCommand(std::string_view name) const
{
    auto it = m_mapCommand.find(name);
    return it != m_mapCommand.cend() ? it->second : nullptr;
}

} // namespace Mayo

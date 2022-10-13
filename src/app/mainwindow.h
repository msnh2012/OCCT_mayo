/****************************************************************************
** Copyright (c) 2021, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "../base/filepath.h"
#include "../base/property.h"
#include "../base/task_manager.h"
#include "../base/text_id.h"
#include <QtWidgets/QMainWindow>
#include <memory>
class QFileInfo;

namespace Mayo {

class Command;
class GuiApplication;
class GuiDocument;
class WidgetGuiDocument;

class AppContext;

class MainWindow : public QMainWindow {
    Q_OBJECT
    MAYO_DECLARE_TEXT_ID_FUNCTIONS(Mayo::MainWindow)
public:
    MainWindow(GuiApplication* guiApp, QWidget* parent = nullptr);
    ~MainWindow();

    void openDocument(const FilePath& fp);
    void openDocumentsFromList(Span<const FilePath> listFilePath);

    bool eventFilter(QObject* watched, QEvent* event) override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    // -- File menu
    void quitApp();
    // -- Display menu
    void toggleCurrentDocOriginTrihedron();
    void toggleCurrentDocPerformanceStats();
    void zoomInCurrentDoc();
    void zoomOutCurrentDoc();
    // -- Tools menu
    void editOptions();
    void saveImageView();
    void inspectXde();
    // -- Window menu
    void toggleLeftSidebar();
    // -- Help menu
    void aboutMayo();
    void reportbug();

    void onApplicationItemSelectionChanged();
    void onOperationFinished(bool ok, const QString& msg);
    void onGuiDocumentAdded(GuiDocument* guiDoc);
    void onWidgetFileSystemLocationActivated(const QFileInfo& loc);
    void onLeftContentsPageChanged(int pageId);
    void onCurrentDocumentIndexChanged(int idx);

    void updateControlsActivation();

    int currentDocumentIndex() const;
    void setCurrentDocumentIndex(int idx);

    WidgetGuiDocument* widgetGuiDocument(int idx) const;
    WidgetGuiDocument* currentWidgetGuiDocument() const;
    QWidget* findLeftHeaderPlaceHolder() const;
    QWidget* recreateLeftHeaderPlaceHolder();
    QMenu* createMenuModelTreeSettings();
    QMenu* createMenuRecentFiles();
    QMenu* createMenuDisplayMode();

    Command* getCommand(std::string_view name) const;
    template<typename CMD>CMD* getCommand() const {
        for (const auto& mapPair : m_mapCommand) {
            auto cmd = dynamic_cast<CMD*>(mapPair.second);
            if (cmd)
                return cmd;
        }

        return nullptr;
    }

    friend class AppContext;

    GuiApplication* m_guiApp = nullptr;
    class Ui_MainWindow* m_ui = nullptr;
    TaskManager m_taskMgr;
    std::unordered_map<std::string_view, Command*> m_mapCommand;
    std::unique_ptr<PropertyGroup> m_ptrCurrentNodeDataProperties;
    std::unique_ptr<PropertyGroupSignals> m_ptrCurrentNodeGraphicsProperties;
};

} // namespace Mayo

/*
    SPDX-FileCopyrightText: 2002 Wilco Greven <greven@kde.org>
    SPDX-FileCopyrightText: 2002 Chris Cheney <ccheney@cheney.cx>
    SPDX-FileCopyrightText: 2003 Benjamin Meyer <benjamin@csh.rit.edu>
    SPDX-FileCopyrightText: 2003-2004 Christophe Devriese <Christophe.Devriese@student.kuleuven.ac.be>
    SPDX-FileCopyrightText: 2003 Laurent Montel <montel@kde.org>
    SPDX-FileCopyrightText: 2003-2004 Albert Astals Cid <aacid@kde.org>
    SPDX-FileCopyrightText: 2003 Luboš Luňák <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2003 Malcolm Hunter <malcolm.hunter@gmx.co.uk>
    SPDX-FileCopyrightText: 2004 Dominique Devriese <devriese@kde.org>
    SPDX-FileCopyrightText: 2004 Dirk Mueller <mueller@kde.org>

    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2017 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "shell.h"

// qt/kde includes
#include <KActionCollection>
#include <KConfigGroup>
#include <KHamburgerMenu>
#include <KIO/Global>
#include <KLocalizedString>
#include <KMessageBox>
#include <KPluginFactory>
#include <KRecentFilesAction>
#include <KSharedConfig>
#include <KStandardAction>
#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
#include <KStartupInfo>
#include <KWindowInfo>
#endif
#include <KToggleFullScreenAction>
#include <KToolBar>
#include <KUrlMimeData>
#include <KWindowSystem>
#include <KXMLGUIFactory>
#include <QApplication>
#if HAVE_DBUS
#include <QDBusConnection>
#endif // HAVE_DBUS
#include <QDockWidget>
#include <QDragMoveEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QSpacerItem>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

// local includes
#include "../interfaces/viewerinterface.h"
#include "ai/aichatpanel.h"
#include "kdocumentviewer.h"
#include "shellutils.h"

static const char *shouldShowMenuBarComingFromFullScreen = "shouldShowMenuBarComingFromFullScreen";
static const char *shouldShowToolBarComingFromFullScreen = "shouldShowToolBarComingFromFullScreen";

static const char *const SESSION_URL_KEY = "Urls";
static const char *const SESSION_TAB_KEY = "ActiveTab";

static constexpr char SIDEBAR_LOCKED_KEY[] = "LockSidebar";
static constexpr char SIDEBAR_VISIBLE_KEY[] = "ShowSidebar";
static constexpr char AI_CHAT_VISIBLE_KEY[] = "ShowAiChat";

static inline QString DesktopEntryGroupKey()
{
    return QStringLiteral("Desktop Entry");
}
static inline QString RecentFilesGroupKey()
{
    return QStringLiteral("Recent Files");
}
static inline QString GeneralGroupKey()
{
    return QStringLiteral("General");
}

class ResizableStackedWidget : public QStackedWidget
{
    Q_OBJECT

public:
    QSize sizeHint() const override
    {
        return currentWidget()->sizeHint();
    }
    QSize minimumSizeHint() const override
    {
        return currentWidget()->minimumSizeHint();
    }
};

/**
 * Groups sidebar containers in a QDockWidget.
 *
 * The dock hosts a custom icon rail on the left and the active part's
 * sidebar content on the right. The part's own tab bar is hidden and driven
 * from the rail.
 */
class Sidebar : public QDockWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr)
        : QDockWidget(parent)
    {
        setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        setFeatures(defaultFeatures());

        auto *container = new QWidget;
        auto *containerLayout = new QHBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        m_rail = new QWidget(container);
        m_rail->setObjectName(QStringLiteral("aiSidebarRail"));
        m_rail->setFixedWidth(46);
        m_railLayout = new QVBoxLayout(m_rail);
        m_railLayout->setContentsMargins(4, 8, 4, 8);
        m_railLayout->setSpacing(4);

        m_menuButton = new QToolButton(m_rail);
        m_menuButton->setObjectName(QStringLiteral("aiRailButton"));
        m_menuButton->setAutoRaise(true);
        m_menuButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu"),
                                              QIcon::fromTheme(QStringLiteral("open-menu-symbolic"), QIcon::fromTheme(QStringLiteral("view-list")))));
        m_menuButton->setToolTip(i18n("Menu"));
        m_menuButton->setAccessibleName(i18n("Menu"));
        m_menuButton->setIconSize(QSize(20, 20));
        m_menuButton->setFixedSize(38, 38);
        m_menuButton->setCursor(Qt::PointingHandCursor);
        connect(m_menuButton, &QToolButton::clicked, this, [this]() {
            Q_EMIT menuRequested(m_menuButton->mapToGlobal(QPoint(m_menuButton->width(), m_menuButton->height())));
        });
        m_railLayout->addWidget(m_menuButton);
        m_railLayout->addSpacing(4);

        m_stackedWidget = new QStackedWidget(container);
        m_stackedWidget->setObjectName(QStringLiteral("aiSidebarContent"));
        containerLayout->addWidget(m_rail);
        containerLayout->addWidget(m_stackedWidget, 1);

        setWidget(container);
        // It seems that without requesting a specific minimum size, Qt
        // somehow calculates a (0,-1) minimum size, and then Qt gets angry
        // that negative sizes is not possible.
        setMinimumSize(10, 10);
    }

    bool isLocked() const
    {
        return features().testFlag(NoDockWidgetFeatures);
    }

    void setLocked(bool locked)
    {
        setFeatures(locked ? NoDockWidgetFeatures : defaultFeatures());

        // show titlebar only if not locked
        if (locked) {
            if (!m_dumbTitleWidget) {
                m_dumbTitleWidget = new QWidget;
            }
            setTitleBarWidget(m_dumbTitleWidget);
        } else {
            // Minimalist look: never show the dock title bar.
            if (!m_emptyTitleWidget) {
                m_emptyTitleWidget = new QWidget;
            }
            setTitleBarWidget(m_emptyTitleWidget);
        }
    }

    int indexOf(QWidget *widget) const
    {
        return m_stackedWidget->indexOf(widget);
    }

    void addWidget(QWidget *widget)
    {
        m_stackedWidget->addWidget(widget);
        attachRail(widget);
    }

    void removeWidget(QWidget *widget)
    {
        m_stackedWidget->removeWidget(widget);
    }

    void setCurrentWidget(QWidget *widget)
    {
        m_stackedWidget->setCurrentWidget(widget);
        attachRail(widget);
    }

Q_SIGNALS:
    void aiRequested();
    void menuRequested(const QPoint &globalPos);

private:
    void attachRail(QWidget *sideContainer)
    {
        QTabWidget *tabs = sideContainer ? sideContainer->findChild<QTabWidget *>() : nullptr;
        if (tabs) {
            // The rail replaces the part's tab bar.
            tabs->tabBar()->setVisible(false);
            tabs->setDocumentMode(true);
        }
        if (m_activeTabs == tabs) {
            return;
        }
        if (m_activeTabs) {
            m_activeTabs->disconnect(this);
        }
        m_activeTabs = tabs;
        rebuildRail();
        if (!tabs) {
            return;
        }
        connect(tabs, &QTabWidget::currentChanged, this, &Sidebar::syncRailSelection);
        syncRailSelection(tabs->currentIndex());
    }

    void rebuildRail()
    {
        for (QToolButton *button : std::as_const(m_railButtons)) {
            m_railLayout->removeWidget(button);
            button->deleteLater();
        }
        m_railButtons.clear();
        if (m_railSpacer) {
            m_railLayout->removeItem(m_railSpacer);
            delete m_railSpacer;
            m_railSpacer = nullptr;
        }
        if (m_aiButton) {
            m_railLayout->removeWidget(m_aiButton);
            m_aiButton->deleteLater();
            m_aiButton = nullptr;
        }

        if (m_activeTabs) {
            for (int index = 0; index < m_activeTabs->count(); ++index) {
                auto *button = new QToolButton(m_rail);
                button->setObjectName(QStringLiteral("aiRailButton"));
                button->setCheckable(true);
                button->setAutoRaise(true);
                button->setIcon(m_activeTabs->tabIcon(index));
                QString tooltip = m_activeTabs->tabToolTip(index);
                if (tooltip.isEmpty()) {
                    tooltip = m_activeTabs->tabText(index);
                }
                button->setToolTip(tooltip);
                button->setAccessibleName(tooltip);
                button->setIconSize(QSize(20, 20));
                button->setFixedSize(38, 38);
                button->setCursor(Qt::PointingHandCursor);
                const int tabIndex = index;
                connect(button, &QToolButton::clicked, this, [this, tabIndex]() {
                    if (m_activeTabs) {
                        m_activeTabs->setCurrentIndex(tabIndex);
                    }
                });
                m_railLayout->addWidget(button);
                m_railButtons.append(button);
            }
        }

        m_railSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
        m_railLayout->addItem(m_railSpacer);

        m_aiButton = new QToolButton(m_rail);
        m_aiButton->setObjectName(QStringLiteral("aiRailButton"));
        m_aiButton->setAutoRaise(true);
        m_aiButton->setIcon(QIcon::fromTheme(QStringLiteral("assistant"), QIcon::fromTheme(QStringLiteral("applications-science"))));
        m_aiButton->setToolTip(i18n("AI mode"));
        m_aiButton->setAccessibleName(i18n("AI mode"));
        m_aiButton->setIconSize(QSize(20, 20));
        m_aiButton->setFixedSize(38, 38);
        m_aiButton->setCursor(Qt::PointingHandCursor);
        connect(m_aiButton, &QToolButton::clicked, this, &Sidebar::aiRequested);
        m_railLayout->addWidget(m_aiButton);
    }

    void syncRailSelection(int index)
    {
        for (int buttonIndex = 0; buttonIndex < m_railButtons.size(); ++buttonIndex) {
            m_railButtons.at(buttonIndex)->setChecked(buttonIndex == index);
        }
    }

    static DockWidgetFeatures defaultFeatures()
    {
        DockWidgetFeatures dockFeatures = DockWidgetClosable | DockWidgetMovable;
        if (!KWindowSystem::isPlatformWayland()) { // TODO : Remove this check when QTBUG-87332 is fixed
            dockFeatures |= DockWidgetFloatable;
        }

        return dockFeatures;
    }

    QWidget *m_rail = nullptr;
    QVBoxLayout *m_railLayout = nullptr;
    QToolButton *m_menuButton = nullptr;
    QList<QToolButton *> m_railButtons;
    QToolButton *m_aiButton = nullptr;
    QSpacerItem *m_railSpacer = nullptr;
    QPointer<QTabWidget> m_activeTabs;
    QStackedWidget *m_stackedWidget = nullptr;
    QWidget *m_dumbTitleWidget = nullptr;
    QWidget *m_emptyTitleWidget = nullptr;
};

Shell::Shell(const QString &serializedOptions)
    : KParts::MainWindow()
    , m_menuBarWasShown(true)
    , m_toolBarWasShown(true)
    , m_isValid(true)
{
    setObjectName(QStringLiteral("okular::Shell#"));
    setContextMenuPolicy(Qt::NoContextMenu);
    // otherwise .rc file won't be found by unit test
    setComponentName(QStringLiteral("okular"), QString());
    // set the shell's ui resource file
    setXMLFile(QStringLiteral("shell.rc"));
    m_fileformatsscanned = false;
    m_showMenuBarAction = nullptr;
    // this routine will find and load our Part.  it finds the Part by
    // name which is a bad idea usually.. but it's alright in this
    // case since our Part is made for this Shell

    const auto result = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf6/parts/okularpart")));

    if (!result) {
        // if we couldn't find our Part, we exit since the Shell by
        // itself can't do anything useful
        m_isValid = false;
        KMessageBox::error(this, i18n("Unable to find the Okular component: %1", result.errorString));
        return;
    } else {
        m_partFactory = result.plugin;
    }

    // now that the Part plugin is loaded, create the part
    KParts::ReadWritePart *const firstPart = m_partFactory->create<KParts::ReadWritePart>(this);
    if (firstPart) {
        // Setup the central widget
        m_centralStackedWidget = new ResizableStackedWidget();
        setCentralWidget(m_centralStackedWidget);

        // Setup the welcome screen
        m_welcomeScreen = new WelcomeScreen(this);
        connect(m_welcomeScreen, &WelcomeScreen::openClicked, this, &Shell::fileOpen);
        connect(m_welcomeScreen, &WelcomeScreen::aiClicked, this, [this]() {
            fileOpen();
            showAiAssistant();
        });
        connect(m_welcomeScreen, &WelcomeScreen::closeClicked, this, &Shell::hideWelcomeScreen);
        connect(m_welcomeScreen, &WelcomeScreen::recentItemClicked, this, [this](const QUrl &url) { openUrl(url); });
        connect(m_welcomeScreen, &WelcomeScreen::forgetRecentItem, this, &Shell::forgetRecentItem);
        m_centralStackedWidget->addWidget(m_welcomeScreen);

        m_welcomeScreen->installEventFilter(this);

        // Setup tab bar
        m_tabWidget = new QTabWidget(this);
        m_tabWidget->setTabsClosable(true);
        m_tabWidget->setElideMode(Qt::ElideRight);
        m_tabWidget->tabBar()->hide();
        m_tabWidget->setDocumentMode(true);
        m_tabWidget->setMovable(true);

        m_tabWidget->setAcceptDrops(true);
        m_tabWidget->tabBar()->installEventFilter(this);

        m_centralStackedWidget->addWidget(m_tabWidget);

        connect(m_tabWidget, &QTabWidget::currentChanged, this, &Shell::setActiveTab);
        connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &Shell::closeTab);
        connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, &Shell::moveTabData);

        m_sidebar = new Sidebar;
        m_sidebar->setObjectName(QStringLiteral("okular_sidebar"));
        m_sidebar->setContextMenuPolicy(Qt::ActionsContextMenu);
        m_sidebar->setWindowTitle(i18n("Sidebar"));
        connect(m_sidebar, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            // sync sidebar visibility with the m_showSidebarAction only if welcome screen is hidden
            if (m_showSidebarAction && m_centralStackedWidget->currentWidget() != m_welcomeScreen) {
                m_showSidebarAction->setChecked(visible);
            }
            if (m_centralStackedWidget->currentWidget() == m_welcomeScreen) {
                // MainWindow tries hard to make its child dockwidgets shown, but during
                // welcome screen we don't want to see the sidebar,
                // so try a bit more to actually hide it.
                m_sidebar->hide();
            }
        });
        addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);
        connect(m_sidebar, &Sidebar::aiRequested, this, &Shell::showAiAssistant);
        connect(m_sidebar, &Sidebar::menuRequested, this, &Shell::showMainMenu);

        m_aiChatPanel = new AiChatPanel(this);
        m_aiChatPanel->setActivePartProvider([this]() -> QObject * {
            if (!m_tabWidget || m_tabs.isEmpty()) {
                return nullptr;
            }
            const int index = m_tabWidget->currentIndex();
            if (index < 0 || index >= m_tabs.size()) {
                return nullptr;
            }
            return m_tabs.at(index).part;
        });

        m_aiChatDock = new QDockWidget(i18n("AI mode"), this);
        m_aiChatDock->setObjectName(QStringLiteral("okular_ai_chat"));
        m_aiChatDock->setWidget(m_aiChatPanel);
        m_aiChatDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        m_aiChatDock->setTitleBarWidget(new QWidget(m_aiChatDock));
        addDockWidget(Qt::RightDockWidgetArea, m_aiChatDock);
        connect(m_aiChatDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            if (m_showAiChatAction) {
                m_showAiChatAction->setChecked(visible);
            }
        });

        // then, setup our actions
        setupActions();
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &QObject::deleteLater);
        // and integrate the part's GUI with the shell's
        setupGUI(Keys | ToolBar | Save);
        applyShellTheme();

        // NOTE : apply default sidebar width only after calling setupGUI(...)
        resizeDocks({m_sidebar}, {200}, Qt::Horizontal);
        resizeDocks({m_aiChatDock}, {440}, Qt::Horizontal);

        m_tabs.append(TabState(firstPart));
        m_tabWidget->addTab(firstPart->widget(), QString()); // triggers setActiveTab that calls createGUI( part )

        connectPart(firstPart);

        readSettings();
        hideWindowChrome();

        m_unique = ShellUtils::unique(serializedOptions);
#if HAVE_DBUS
        if (m_unique) {
            m_unique = QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.okular"));
            if (!m_unique) {
                KMessageBox::information(this, i18n("There is already a unique Okular instance running. This instance won't be the unique one."));
            }
        } else {
            // TODO When porting to KF7 Remove
            // PID is not unique in containers and "-" in the name violates D-Bus naming conventions.
            // Was left for compatibility with 3rd-party scripts.
            QString serviceName = QStringLiteral("org.kde.okular-") + QString::number(qApp->applicationPid());
            QDBusConnection::sessionBus().registerService(serviceName);

            QDBusConnection::sessionBus().registerService(ShellUtils::currentProcessDbusName());
        }
        if (ShellUtils::noRaise(serializedOptions)) {
            setAttribute(Qt::WA_ShowWithoutActivating);
        }

        {
            const QString editorCmd = ShellUtils::editorCmd(serializedOptions);
            if (!editorCmd.isEmpty()) {
                QMetaObject::invokeMethod(firstPart, "setEditorCmd", Q_ARG(QString, editorCmd));
            }
        }

        QDBusConnection::sessionBus().registerObject(QStringLiteral("/okularshell"), this, QDBusConnection::ExportScriptableSlots);
#endif // HAVE_DBUS

        // Make sure that the welcome scren is visible on startup.
        showWelcomeScreen();
    } else {
        m_isValid = false;
        KMessageBox::error(this, i18n("Unable to find the Okular component."));
    }

    connect(guiFactory(), &KXMLGUIFactory::shortcutsSaved, this, &Shell::reloadAllXML);
}

void Shell::reloadAllXML()
{
    for (const TabState &tab : std::as_const(m_tabs)) {
        tab.part->reloadXML();
    }
}

void Shell::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && window()->isFullScreen()) {
        setFullScreen(false);
    }
}

bool Shell::eventFilter(QObject *obj, QEvent *event)
{
    QDragMoveEvent *dmEvent = dynamic_cast<QDragMoveEvent *>(event);
    if (dmEvent) {
        bool accept = dmEvent->mimeData()->hasUrls();
        event->setAccepted(accept);
        return accept;
    }

    QDropEvent *dEvent = dynamic_cast<QDropEvent *>(event);
    if (dEvent) {
        const QList<QUrl> list = KUrlMimeData::urlsFromMimeData(dEvent->mimeData());
        handleDroppedUrls(list);
        dEvent->setAccepted(true);
        return true;
    }

    // Handle middle button click events on the tab bar
    if (obj == m_tabWidget->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mEvent = static_cast<QMouseEvent *>(event);
        if (mEvent->button() == Qt::MiddleButton) {
            int tabIndex = m_tabWidget->tabBar()->tabAt(mEvent->pos());
            if (tabIndex != -1) {
                closeTab(tabIndex);
                return true;
            }
        }
    }
    return KParts::MainWindow::eventFilter(obj, event);
}

bool Shell::isValid() const
{
    return m_isValid;
}

void Shell::showOpenRecentMenu()
{
    m_recent->menu()->popup(QCursor::pos());
}

Shell::~Shell()
{
    if (!m_tabs.empty()) {
        writeSettings();
        for (const TabState &tab : std::as_const(m_tabs)) {
            tab.part->closeUrl(false);
        }
        m_tabs.clear();
    }
#if HAVE_DBUS
    if (m_unique) {
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.okular"));
    }
#endif // HAVE_DBUS

    delete m_tabWidget;
}

// Open a new document if we have space for it
// This can hang if called on a unique instance and openUrl pops a messageBox
bool Shell::openDocument(const QUrl &url, const QString &serializedOptions)
{
    if (m_tabs.size() <= 0) {
        return false;
    }

    hideWelcomeScreen();

    KParts::ReadWritePart *const part = m_tabs[0].part;

    // Return false if we can't open new tabs and the only part is occupied
    if (!qobject_cast<Okular::ViewerInterface *>(part)->openNewFilesInTabs() && !part->url().isEmpty() && !ShellUtils::unique(serializedOptions)) {
        return false;
    }

    openUrl(url, serializedOptions);

    return true;
}

bool Shell::openDocument(const QString &urlString, const QString &serializedOptions)
{
    return openDocument(QUrl(urlString), serializedOptions);
}

void Shell::openNewlySignedFile(const QString &path, int pageNumber)
{
    // for now, this function just applies the "replace current document"
    // strategy for opening.
    // Given signing is slightly closer to annotating and saving, the over
    // all user experience should not be that different
    // The fact that we get a different file out of saving is a bit of a
    // implementation  detail that shouldn't leak that much onto the users
    QUrl url = QUrl::fromLocalFile(path);
    url.setFragment(QStringLiteral("page=%1").arg(pageNumber));

    const int activeTab = m_tabWidget->currentIndex();
    KParts::ReadWritePart *const activePart = m_tabs[activeTab].part;
    activePart->closeUrl(false);
    activePart->openUrl(url);

    if (m_aiChatPanel) {
        m_aiChatPanel->documentChanged();
    }
}

bool Shell::canOpenDocs(int numDocs, int desktop)
{
    if (m_tabs.size() <= 0 || numDocs <= 0 || m_unique) {
        return false;
    }

    KParts::ReadWritePart *const part = m_tabs[0].part;
    const bool allowTabs = qobject_cast<Okular::ViewerInterface *>(part)->openNewFilesInTabs();

    if (!allowTabs && (numDocs > 1 || !part->url().isEmpty())) {
        return false;
    }

#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
    const KWindowInfo winfo(window()->effectiveWinId(), NET::WMDesktop);
    if (winfo.desktop() != desktop) {
        return false;
    }
#else
    Q_UNUSED(desktop);
#endif

    return true;
}

void Shell::openUrl(const QUrl &url, const QString &serializedOptions)
{
    hideWelcomeScreen();

    const int activeTab = m_tabWidget->currentIndex();
    KParts::ReadWritePart *const activePart = m_tabs[activeTab].part;
    if (!activePart->url().isEmpty()) {
        if (m_unique) {
            applyOptionsToPart(activePart, serializedOptions);
            activePart->openUrl(url);
        } else {
            if (qobject_cast<Okular::ViewerInterface *>(activePart)->openNewFilesInTabs()) {
                openNewTab(url, serializedOptions);
            } else {
                Shell *newShell = new Shell(serializedOptions);
                newShell->show();
                newShell->openUrl(url, serializedOptions);
            }
        }
    } else {
        m_tabWidget->setTabText(activeTab, url.fileName());
        m_tabWidget->setTabToolTip(activeTab, url.fileName());

        applyOptionsToPart(activePart, serializedOptions);
        bool openOk = activePart->openUrl(url);
        const bool isstdin = url.fileName() == QLatin1String("-") || url.scheme() == QLatin1String("fd");
        if (!isstdin) {
            if (openOk) {
                m_recent->addUrl(url);
            } else {
                m_recent->removeUrl(url);
                closeTab(activeTab);
            }
        }
    }

    if (m_aiChatPanel) {
        m_aiChatPanel->documentChanged();
    }
}

void Shell::closeUrl()
{
    closeTab(m_tabWidget->currentIndex());

    // When closing the current tab two things can happen:
    //  * the focus was on the tab
    //  * the focus was somewhere in the toolbar
    // we don't have other places that accept focus
    //  * If it was on the tab, logic says it should go back to the next current tab
    //  * If it was on the toolbar, we could leave it there, but since we redo the menus/toolbars for the new tab, it gets kind of lost
    //    so it's easier to set it to the next current tab which also makes sense as consistency
    if (m_tabWidget->count() >= 0) {
        KParts::ReadWritePart *const newPart = m_tabs[m_tabWidget->currentIndex()].part;
        newPart->widget()->setFocus();
    }
}

void Shell::readSettings()
{
    readRecentFilesSettings();

    const KConfigGroup group = KSharedConfig::openConfig()->group(DesktopEntryGroupKey());
    bool fullScreen = group.readEntry("FullScreen", false);
    setFullScreen(fullScreen);

    if (fullScreen) {
        m_menuBarWasShown = group.readEntry(shouldShowMenuBarComingFromFullScreen, true);
        m_toolBarWasShown = group.readEntry(shouldShowToolBarComingFromFullScreen, true);
    }

    // Minimalist default: the menu bar is hidden, the hamburger menu remains.
    const bool showMenuBar = group.readEntry("ShowMenuBar", false);
    if (m_showMenuBarAction && m_showMenuBarAction->isChecked() != showMenuBar) {
        m_showMenuBarAction->setChecked(showMenuBar);
    }

    const KConfigGroup sidebarGroup = KSharedConfig::openConfig()->group(GeneralGroupKey());
    m_sidebar->setVisible(sidebarGroup.readEntry(SIDEBAR_VISIBLE_KEY, true));
    m_sidebar->setLocked(sidebarGroup.readEntry(SIDEBAR_LOCKED_KEY, true));

    m_showSidebarAction->setChecked(m_sidebar->isVisibleTo(this));
    m_lockSidebarAction->setChecked(m_sidebar->isLocked());

    const bool showAiChat = sidebarGroup.readEntry(AI_CHAT_VISIBLE_KEY, true);
    if (m_aiChatDock) {
        m_aiChatDock->setVisible(showAiChat);
    }
    if (m_showAiChatAction) {
        m_showAiChatAction->setChecked(showAiChat);
    }
}

void Shell::writeSettings()
{
    saveRecents();

    KConfigGroup sidebarGroup = KSharedConfig::openConfig()->group(GeneralGroupKey());
    sidebarGroup.writeEntry(SIDEBAR_LOCKED_KEY, m_sidebar->isLocked());
    // NOTE : Consider whether the m_showSidebarAction is checked, because
    // the sidebar can be forcibly hidden if the welcome screen is displayed
    sidebarGroup.writeEntry(SIDEBAR_VISIBLE_KEY, m_sidebar->isVisibleTo(this) || m_showSidebarAction->isChecked());
    sidebarGroup.writeEntry(AI_CHAT_VISIBLE_KEY, (m_aiChatDock && m_aiChatDock->isVisibleTo(this)) || (m_showAiChatAction && m_showAiChatAction->isChecked()));

    KConfigGroup group = KSharedConfig::openConfig()->group(DesktopEntryGroupKey());
    group.writeEntry("FullScreen", m_fullScreenAction->isChecked());
    group.writeEntry("ShowMenuBar", m_showMenuBarAction->isChecked());
    if (m_fullScreenAction->isChecked()) {
        group.writeEntry(shouldShowMenuBarComingFromFullScreen, m_menuBarWasShown);
        group.writeEntry(shouldShowToolBarComingFromFullScreen, m_toolBarWasShown);
    }
    KSharedConfig::openConfig()->sync();
}

void Shell::saveRecents()
{
    m_recent->saveEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

void Shell::setupActions()
{
    KStandardAction::open(this, SLOT(fileOpen()), actionCollection());
    m_recent = KStandardAction::openRecent(this, SLOT(openUrl(QUrl)), actionCollection());
    m_recent->setToolBarMode(KRecentFilesAction::MenuMode);
    connect(m_recent, &QAction::triggered, this, &Shell::showOpenRecentMenu);
    connect(m_recent, &KRecentFilesAction::recentListCleared, this, &Shell::refreshRecentsOnWelcomeScreen);
    connect(m_welcomeScreen, &WelcomeScreen::forgetAllRecents, m_recent, &KRecentFilesAction::clear);
    m_recent->setToolTip(i18n("Click to open a file\nClick and hold to open a recent file"));
    m_recent->setWhatsThis(i18n("<b>Click</b> to open a file or <b>Click and hold</b> to select a recent file"));
    m_printAction = KStandardAction::print(this, SLOT(print()), actionCollection());
    m_printAction->setEnabled(false);
    m_closeAction = KStandardAction::close(this, SLOT(closeUrl()), actionCollection());
    m_closeAction->setEnabled(false);
    KStandardAction::quit(this, SLOT(close()), actionCollection());

    setStandardToolBarMenuEnabled(true);

    m_showMenuBarAction = KStandardAction::showMenubar(this, SLOT(slotShowMenubar()), actionCollection());
    m_fullScreenAction = KStandardAction::fullScreen(this, SLOT(slotUpdateFullScreen()), this, actionCollection());

    actionCollection()->setDefaultShortcuts(m_fullScreenAction, KStandardShortcut::fullScreen() + QList {QKeySequence(Qt::Key_F11)});

    m_nextTabAction = actionCollection()->addAction(QStringLiteral("tab-next"));
    m_nextTabAction->setText(i18n("Next Tab"));
    actionCollection()->setDefaultShortcuts(m_nextTabAction, KStandardShortcut::tabNext());
    m_nextTabAction->setEnabled(false);
    connect(m_nextTabAction, &QAction::triggered, this, &Shell::activateNextTab);

    m_prevTabAction = actionCollection()->addAction(QStringLiteral("tab-previous"));
    m_prevTabAction->setText(i18n("Previous Tab"));
    actionCollection()->setDefaultShortcuts(m_prevTabAction, KStandardShortcut::tabPrev());
    m_prevTabAction->setEnabled(false);
    connect(m_prevTabAction, &QAction::triggered, this, &Shell::activatePrevTab);

    // add shortcuts for Shift+Alt+1 to Shift+Alt+9 to switch tabs(browser logic- Shift+Alt+9 is always going to be last tab)
    for (int i = 1; i <= 9; ++i) {
        QAction *action = actionCollection()->addAction(QStringLiteral("tab-switch-%1").arg(i));
        action->setText(i18n("Switch to Tab %1", i));

        // static cast to Qt::Key satisfies Qt 6 strict type checking for QKeySequence
        actionCollection()->setDefaultShortcut(action, QKeySequence(Qt::SHIFT | Qt::ALT | static_cast<Qt::Key>(Qt::Key_0 + i)));
        connect(action, &QAction::triggered, this, [this, i]() {
            if (m_tabs.isEmpty()) {
                return;
            }
            int index = (i == 9) ? m_tabs.size() - 1 : i - 1;
            if (index >= 0 && index < m_tabs.size()) {
                setActiveTab(index);
            }
        });
    }

    m_undoCloseTab = actionCollection()->addAction(QStringLiteral("undo-close-tab"));
    m_undoCloseTab->setText(i18n("Undo close tab"));
    actionCollection()->setDefaultShortcut(m_undoCloseTab, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    m_undoCloseTab->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    m_undoCloseTab->setEnabled(false);
    connect(m_undoCloseTab, &QAction::triggered, this, &Shell::undoCloseTab);

    m_lockSidebarAction = actionCollection()->addAction(QStringLiteral("okular_lock_sidebar"));
    m_lockSidebarAction->setCheckable(true);
    m_lockSidebarAction->setIcon(QIcon::fromTheme(QStringLiteral("lock")));
    m_lockSidebarAction->setText(i18n("Lock Sidebar"));
    connect(m_lockSidebarAction, &QAction::triggered, m_sidebar, &Sidebar::setLocked);
    m_sidebar->addAction(m_lockSidebarAction);

    m_showAiChatAction = actionCollection()->addAction(QStringLiteral("view_ai_chat"));
    m_showAiChatAction->setText(i18n("AI mode"));
    m_showAiChatAction->setCheckable(true);
    m_showAiChatAction->setIcon(QIcon::fromTheme(QStringLiteral("assistant"), QIcon::fromTheme(QStringLiteral("applications-science"))));
    m_showAiChatAction->setToolTip(i18n("AI mode"));
    actionCollection()->setDefaultShortcut(m_showAiChatAction, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    connect(m_showAiChatAction, &QAction::triggered, this, [this](bool checked) {
        if (m_aiChatDock) {
            m_aiChatDock->setVisible(checked);
        }
    });
}

void Shell::saveProperties(KConfigGroup &group)
{
    if (!m_isValid) { // part couldn't be loaded, nothing to save
        return;
    }

    // Gather lists of settings to preserve
    QStringList urls;
    for (const TabState &tab : std::as_const(m_tabs)) {
        urls.append(tab.part->url().url());
    }
    group.writePathEntry(SESSION_URL_KEY, urls);
    group.writeEntry(SESSION_TAB_KEY, m_tabWidget->currentIndex());
}

void Shell::readProperties(const KConfigGroup &group)
{
    // Reopen documents based on saved settings
    QStringList urls = group.readPathEntry(SESSION_URL_KEY, QStringList());
    int desiredTab = group.readEntry<int>(SESSION_TAB_KEY, 0);

    while (!urls.isEmpty()) {
        openUrl(QUrl(urls.takeFirst()));
    }

    if (desiredTab < m_tabs.size()) {
        setActiveTab(desiredTab);
    }
}

void Shell::fileOpen()
{
    // this slot is called whenever the File->Open menu is selected,
    // the Open shortcut is pressed (usually CTRL+O) or the Open toolbar
    // button is clicked
    const int activeTab = m_tabWidget->currentIndex();
    if (!m_fileformatsscanned) {
        const KDocumentViewer *const doc = qobject_cast<KDocumentViewer *>(m_tabs[activeTab].part);
        Q_ASSERT(doc);

        m_fileformats = doc->supportedMimeTypes();

        m_fileformatsscanned = true;
    }

    QUrl startDir;
    const KParts::ReadWritePart *const curPart = m_tabs[activeTab].part;
    if (curPart->url().isLocalFile()) {
        startDir = KIO::upUrl(curPart->url());
    }
    if (startDir.isEmpty() || (startDir == QUrl::fromLocalFile(QDir::rootPath()))) {
        startDir = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }

    QPointer<QFileDialog> dlg(new QFileDialog(this));
    dlg->setDirectoryUrl(startDir);
    dlg->setAcceptMode(QFileDialog::AcceptOpen);
    dlg->setOption(QFileDialog::HideNameFilterDetails, true);
    dlg->setFileMode(QFileDialog::ExistingFiles); // Allow selection of more than one file

    QMimeDatabase mimeDatabase;
    // Unfortunately non Plasma file dialogs don't support the "All supported files" when using
    // setMimeTypeFilters instead of setNameFilters, so for those use setNameFilters which is a bit
    // worse because doesn't show you pdf files named bla.blo when you say "show me the pdf files", but
    // that's solvable by choosing "All Files" and it's not that common while it's more convenient to
    // only get shown the files that the application can open by default instead of all of them
    const bool useMimeTypeFilters = qgetenv("XDG_CURRENT_DESKTOP").toLower() == "kde";
    if (useMimeTypeFilters) {
        QStringList mimetypes;
        for (const QString &mimeName : std::as_const(m_fileformats)) {
            QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeName);
            mimetypes << mimeType.name();
        }
        mimetypes.prepend(QStringLiteral("application/octet-stream"));
        dlg->setMimeTypeFilters(mimetypes);
    } else {
        QSet<QString> globPatterns;
        QMap<QString, QStringList> namedGlobs;
        for (const QString &mimeName : std::as_const(m_fileformats)) {
            QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeName);
            const QStringList globs(mimeType.globPatterns());
            if (globs.isEmpty()) {
                continue;
            }

            globPatterns.unite(QSet<QString>(globs.begin(), globs.end()));

            namedGlobs[mimeType.comment()].append(globs);
        }
        QStringList namePatterns;
        for (auto it = namedGlobs.cbegin(); it != namedGlobs.cend(); ++it) {
            namePatterns.append(it.key() + QLatin1String(" (") + it.value().join(QLatin1Char(' ')) + QLatin1Char(')'));
        }

        const QStringList allGlobPatterns = globPatterns.values();
        namePatterns.prepend(i18n("All files (*)"));
        namePatterns.prepend(i18n("All supported files (%1)", allGlobPatterns.join(QLatin1Char(' '))));
        dlg->setNameFilters(namePatterns);
    }

    dlg->setWindowTitle(i18n("Open Document")); /* cppcheck-suppress nullPointerRedundantCheck ; QPointer things here is not understood*/
    if (dlg->exec() && dlg) {                   /* cppcheck-suppress nullPointerRedundantCheck ; QPointer things here is not understood*/
        const QList<QUrl> urlList = dlg->selectedUrls();
        for (const QUrl &url : urlList) {
            openUrl(url);
        }
    }

    if (dlg) {
        delete dlg.data();
    }
}

void Shell::tryRaise(const QString &startupId)
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
    if (KWindowSystem::isPlatformWayland()) {
        KWindowSystem::setCurrentXdgActivationToken(startupId);
    } else if (KWindowSystem::isPlatformX11()) {
        KStartupInfo::setNewStartupId(window()->windowHandle(), startupId.toUtf8());
    }
#else
    Q_UNUSED(startupId);
#endif

    KWindowSystem::activateWindow(window()->windowHandle());
}

// only called when starting the program
void Shell::setFullScreen(bool useFullScreen)
{
    if (useFullScreen) {
        setWindowState(windowState() | Qt::WindowFullScreen); // set
    } else {
        setWindowState(windowState() & ~Qt::WindowFullScreen); // reset
    }
}

void Shell::setCaption(const QString &caption)
{
    bool modified = false;

    const int activeTab = m_tabWidget->currentIndex();
    if (activeTab != -1) {
        KParts::ReadWritePart *const activePart = m_tabs[activeTab].part;
        QString tabCaption = activePart->url().fileName();
        if (activePart->isModified()) {
            modified = true;
            if (!tabCaption.isEmpty()) {
                tabCaption.append(QStringLiteral(" *"));
            }
        }

        m_tabWidget->setTabText(activeTab, tabCaption);
    }

    setCaption(caption, modified);
}

void Shell::showEvent(QShowEvent *e)
{
    if (!menuBar()->isNativeMenuBar() && m_showMenuBarAction) {
        m_showMenuBarAction->setChecked(menuBar()->isVisible());
    }

    KParts::MainWindow::showEvent(e);
}

void Shell::slotUpdateFullScreen()
{
    if (m_fullScreenAction->isChecked()) {
        m_menuBarWasShown = !menuBar()->isHidden();
        menuBar()->hide();

        m_toolBarWasShown = !toolBar()->isHidden();
        toolBar()->hide();

        KToggleFullScreenAction::setFullScreen(this, true);
    } else {
        if (m_menuBarWasShown) {
            menuBar()->show();
        }
        if (m_toolBarWasShown) {
            toolBar()->show();
        }
        KToggleFullScreenAction::setFullScreen(this, false);
    }
}

void Shell::slotShowMenubar()
{
    if (menuBar()->isHidden()) {
        menuBar()->show();
    } else {
        menuBar()->hide();
    }
}

QSize Shell::sizeHint() const
{
    const QSize baseSize = QApplication::primaryScreen()->availableSize() * 0.6;
    // Set an arbitrary yet sensible sane minimum size for very small screens;
    // for example we don't want people using 1366x768 screens to get a tiny
    // default window size of 820 x 460 which will elide most of the toolbar buttons.
    return baseSize.expandedTo(QSize(1000, 700));
}

bool Shell::queryClose()
{
    if (m_tabs.count() > 1) {
        const QString dontAskAgainName = QStringLiteral("ShowTabWarning");
        KMessageBox::ButtonCode dummy = KMessageBox::PrimaryAction;
        if (KMessageBox::shouldBeShownTwoActions(dontAskAgainName, dummy)) {
            QDialog *dialog = new QDialog(this);
            dialog->setWindowTitle(i18n("Confirm Close"));

            QDialogButtonBox *buttonBox = new QDialogButtonBox(dialog);
            buttonBox->setStandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No);
            KGuiItem::assign(buttonBox->button(QDialogButtonBox::Yes), KGuiItem(i18n("Close Tabs"), QStringLiteral("tab-close")));
            KGuiItem::assign(buttonBox->button(QDialogButtonBox::No), KStandardGuiItem::cancel());

            bool checkboxResult = true;
            const int result = KMessageBox::createKMessageBox(dialog,
                                                              buttonBox,
                                                              QMessageBox::Question,
                                                              i18n("You are about to close %1 tabs. Are you sure you want to continue?", m_tabs.count()),
                                                              QStringList(),
                                                              i18n("Warn me when I attempt to close multiple tabs"),
                                                              &checkboxResult,
                                                              KMessageBox::Notify);

            if (!checkboxResult) {
                KMessageBox::saveDontShowAgainTwoActions(dontAskAgainName, dummy);
            }

            if (result != QDialogButtonBox::Yes) {
                return false;
            }
        }
    }

    for (int i = 0; i < m_tabs.size(); ++i) {
        KParts::ReadWritePart *const part = m_tabs[i].part;

        // To resolve confusion about multiple modified docs, switch to relevant tab
        if (part->isModified()) {
            setActiveTab(i);
        }

        if (!part->queryClose()) {
            return false;
        }
    }
    return true;
}

void Shell::setActiveTab(int tab)
{
    if (m_showSidebarAction) {
        m_showSidebarAction->disconnect(m_sidebar);
    }

    m_tabWidget->setCurrentIndex(tab);

    // NOTE : createGUI(...) breaks the visibility of the sidebar, so we need
    // to save and restore it
    const bool isSidebarVisible = m_sidebar->isVisible();
    const bool isAiChatVisible = m_aiChatDock && m_aiChatDock->isVisible();
    createGUI(m_tabs[tab].part);
    m_sidebar->setVisible(isSidebarVisible);
    if (m_aiChatDock) {
        m_aiChatDock->setVisible(isAiChatVisible);
    }

    // dock KPart's sidebar if new and make it current
    Okular::ViewerInterface *iPart = qobject_cast<Okular::ViewerInterface *>(m_tabs[tab].part);
    Q_ASSERT(iPart);
    QWidget *sideContainer = iPart->getSideContainer();
    if (m_sidebar->indexOf(sideContainer) == -1) {
        m_sidebar->addWidget(sideContainer);
        if (m_sidebar->maximumWidth() > sideContainer->maximumWidth()) {
            m_sidebar->setMaximumWidth(sideContainer->maximumWidth());
        }
    }
    m_sidebar->setCurrentWidget(sideContainer);

    m_showSidebarAction = m_tabs[tab].part->actionCollection()->action(QStringLiteral("show_leftpanel"));
    Q_ASSERT(m_showSidebarAction);
    m_showSidebarAction->disconnect(m_sidebar);
    m_showSidebarAction->setChecked(m_sidebar->isVisibleTo(this));
    connect(m_showSidebarAction, &QAction::triggered, m_sidebar, &Sidebar::setVisible);

    m_printAction->setEnabled(m_tabs[tab].printEnabled);
    m_closeAction->setEnabled(m_tabs[tab].closeEnabled);

    if (m_aiChatPanel) {
        m_aiChatPanel->documentChanged();
    }

    updateToolbarExtras();
    hideWindowChrome();
}

void Shell::hideWindowChrome()
{
    // createGUI() can restore toolbar visibility, so this is also called from
    // setActiveTab(). The menu bar stays hidden unless the user explicitly
    // enables it from the hamburger menu.
    if (menuBar() && (!m_showMenuBarAction || !m_showMenuBarAction->isChecked())) {
        menuBar()->hide();
    }
    if (QToolBar *mainToolBar = toolBar(QStringLiteral("mainToolBar"))) {
        mainToolBar->hide();
    }
}

void Shell::showMainMenu(const QPoint &globalPos)
{
    QAction *hamburger = actionCollection()->action(QStringLiteral("hamburger_menu"));
    if (!hamburger && m_tabWidget && m_tabWidget->currentIndex() >= 0 && m_tabWidget->currentIndex() < m_tabs.size()) {
        if (auto *part = m_tabs.at(m_tabWidget->currentIndex()).part) {
            hamburger = part->actionCollection()->action(QStringLiteral("hamburger_menu"));
        }
    }
    if (auto *menuAction = qobject_cast<KHamburgerMenu *>(hamburger)) {
        // KHamburgerMenu opens at the cursor position when it has no visible
        // toolbar button. Its aboutToShowMenu signal rebuilds the menu first.
        menuAction->trigger();
        return;
    }

    // Fallback menu for documents whose part does not provide a hamburger.
    QMenu menu(this);
    auto add = [&menu](QAction *action) {
        if (action) {
            menu.addAction(action);
        }
    };
    add(actionCollection()->action(KStandardAction::name(KStandardAction::Open)));
    if (m_recent) {
        if (QMenu *recentMenu = m_recent->menu()) {
            recentMenu->setTitle(i18n("Open Recent"));
            menu.addMenu(recentMenu);
        }
    }
    add(m_printAction);
    add(m_closeAction);
    menu.addSeparator();
    add(m_undoCloseTab);
    add(m_showSidebarAction);
    add(m_showAiChatAction);
    add(m_fullScreenAction);
    menu.addSeparator();
    add(actionCollection()->action(QStringLiteral("options_configure")));
    add(actionCollection()->action(KStandardAction::name(KStandardAction::Quit)));
    menu.exec(globalPos);
}

void Shell::showAiAssistant()
{
    if (!m_aiChatDock) {
        return;
    }
    m_aiChatDock->setVisible(true);
    m_aiChatDock->raise();
    if (m_aiChatPanel) {
        m_aiChatPanel->focusComposer();
    }
}

void Shell::handleAiQuery(const QString &prompt)
{
    showAiAssistant();
    if (m_aiChatPanel) {
        m_aiChatPanel->askFromOutside(prompt);
    }
}

void Shell::updateToolbarExtras()
{
    QToolBar *mainToolBar = toolBar(QStringLiteral("mainToolBar"));
    if (!mainToolBar || !m_showAiChatAction) {
        return;
    }

    // Minimalist toolbar: icons only, no text, fixed in place.
    mainToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mainToolBar->setIconSize(QSize(20, 20));
    mainToolBar->setMovable(false);
    mainToolBar->setFloatable(false);

    const QList<QAction *> actions = mainToolBar->actions();

    // Hamburger goes first.
    QAction *hamburger = nullptr;
    for (QAction *action : actions) {
        if (action->objectName() == QLatin1String("hamburger_menu")) {
            hamburger = action;
            break;
        }
    }
    if (hamburger && !mainToolBar->actions().isEmpty() && mainToolBar->actions().constFirst() != hamburger) {
        mainToolBar->removeAction(hamburger);
        mainToolBar->insertAction(mainToolBar->actions().value(0), hamburger);
    }

    // AI toggle pinned to the right: expanding spacer + action.
    QWidget *spacer = mainToolBar->findChild<QWidget *>(QStringLiteral("aiToolbarSpacer"));
    if (!spacer) {
        spacer = new QWidget(mainToolBar);
        spacer->setObjectName(QStringLiteral("aiToolbarSpacer"));
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        mainToolBar->addWidget(spacer);
    }
    if (!mainToolBar->actions().contains(m_showAiChatAction)) {
        mainToolBar->addAction(m_showAiChatAction);
    }
}

void Shell::changeEvent(QEvent *event)
{
    KParts::MainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        applyShellTheme();
    }
}

void Shell::applyShellTheme()
{
    if (m_applyingShellTheme) {
        return;
    }
    m_applyingShellTheme = true;

    const QPalette p = palette();
    const QColor window = p.color(QPalette::Window);
    const QColor text = p.color(QPalette::WindowText);
    const QColor base = p.color(QPalette::Base);
    const bool dark = window.lightness() < 128;
    const QColor surface = dark ? window.lighter(112) : base.darker(102);
    const QColor hover = dark ? window.lighter(124) : window.darker(107);
    const QColor border = p.color(QPalette::Mid);
    const QColor muted = p.color(QPalette::Disabled, QPalette::WindowText);
    const QColor accent = p.color(QPalette::Highlight);

    auto rgba = [](const QColor &color, int alpha) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(alpha);
    };

    QString css = QStringLiteral(R"(
QMainWindow, QMainWindow > QWidget { background: @window; }
QToolBar { background: transparent; border: none; spacing: 2px; padding: 4px 8px; }
QToolBar QToolButton { border: none; border-radius: 10px; padding: 5px; background: transparent; }
QToolBar QToolButton:hover { background: @hover; }
QToolBar QToolButton:pressed { background: @hover; }
QToolBar QToolButton:checked { background: @accentSoft; color: @accent; }
QMenuBar { background: transparent; border: none; }
QMenuBar::item { border-radius: 8px; padding: 4px 10px; }
QMenuBar::item:selected { background: @hover; }
QMenu { background: @surface; color: @text; border: 1px solid @border; border-radius: 12px; padding: 6px; }
QMenu::item { padding: 5px 18px; border-radius: 8px; }
QMenu::item:selected { background: @hover; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QWidget#aiSidebarRail { background: transparent; }
QWidget#aiSidebarContent { background: transparent; }
QToolButton#aiRailButton { border: none; border-radius: 10px; background: transparent; }
QToolButton#aiRailButton:hover { background: @hover; }
QToolButton#aiRailButton:checked { background: @accentSoft; }
QTabWidget::pane { border: none; }
QDockWidget { border: none; }
QSplitter::handle { background: transparent; }
QStatusBar { background: transparent; border: none; color: @muted; }
QStatusBar::item { border: none; }
QToolTip { background: @surface; color: @text; border: 1px solid @border; padding: 4px 6px; border-radius: 6px; }
)");

    css.replace(QStringLiteral("@window"), window.name());
    css.replace(QStringLiteral("@text"), text.name());
    css.replace(QStringLiteral("@muted"), rgba(muted, 200));
    css.replace(QStringLiteral("@surface"), surface.name());
    css.replace(QStringLiteral("@hover"), hover.name());
    css.replace(QStringLiteral("@border"), rgba(border, 110));
    css.replace(QStringLiteral("@accentSoft"), rgba(accent, dark ? 60 : 35));
    css.replace(QStringLiteral("@accent"), accent.name());

    setStyleSheet(css);
    m_applyingShellTheme = false;
}

void Shell::closeTab(int tab)
{
    KParts::ReadWritePart *const part = m_tabs[tab].part;
    QUrl url = part->url();
    bool closeSuccess = part->closeUrl();
    if (closeSuccess && m_tabs.count() > 1) {
        if (part->factory()) {
            part->factory()->removeClient(part);
        }
        part->disconnect(this);

        Okular::ViewerInterface *iPart = qobject_cast<Okular::ViewerInterface *>(m_tabs[tab].part);
        Q_ASSERT(iPart);
        QWidget *sideContainer = iPart->getSideContainer();
        m_sidebar->removeWidget(sideContainer);
        connect(part, &QObject::destroyed, sideContainer, &QObject::deleteLater);

        part->deleteLater();
        m_tabs.removeAt(tab);
        m_tabWidget->removeTab(tab);
        m_undoCloseTab->setEnabled(true);
        m_closedTabUrls.append(url);

        if (m_tabWidget->count() == 1) {
            m_tabWidget->tabBar()->hide();
            m_nextTabAction->setEnabled(false);
            m_prevTabAction->setEnabled(false);
        }
    } else if (closeSuccess && m_tabs.count() == 1) {
        // Show welcome screen when the last tab is closed.

        showWelcomeScreen();
    }
}

void Shell::openNewTab(const QUrl &url, const QString &serializedOptions)
{
    const int previousActiveTab = m_tabWidget->currentIndex();
    KParts::ReadWritePart *const activePart = m_tabs[previousActiveTab].part;

    hideWelcomeScreen();

    bool activateTabIfAlreadyOpen;
    QMetaObject::invokeMethod(activePart, "activateTabIfAlreadyOpenFile", Q_RETURN_ARG(bool, activateTabIfAlreadyOpen));

    if (activateTabIfAlreadyOpen) {
        const int tabIndex = findTabIndex(url);

        if (tabIndex >= 0) {
            setActiveTab(tabIndex);
            m_recent->addUrl(url);
            return;
        }
    }

    // Tabs are hidden when there's only one, so show it
    if (m_tabs.size() == 1) {
        m_tabWidget->tabBar()->show();
        m_nextTabAction->setEnabled(true);
        m_prevTabAction->setEnabled(true);
    }

    const int newIndex = m_tabs.size();

    // Make new part
    m_tabs.append(TabState(m_partFactory->create<KParts::ReadWritePart>(this)));
    connectPart(m_tabs[newIndex].part);

    // Update GUI
    KParts::ReadWritePart *const part = m_tabs[newIndex].part;
    m_tabWidget->addTab(part->widget(), url.fileName());
    m_tabWidget->setTabToolTip(newIndex, url.fileName());

    applyOptionsToPart(part, serializedOptions);

    setActiveTab(m_tabs.size() - 1);

    if (part->openUrl(url)) {
        m_recent->addUrl(url);
    } else {
        setActiveTab(previousActiveTab);
        closeTab(m_tabs.size() - 1);
        m_recent->removeUrl(url);
    }
}

void Shell::applyOptionsToPart(QObject *part, const QString &serializedOptions)
{
    KDocumentViewer *const doc = qobject_cast<KDocumentViewer *>(part);
    const QString find = ShellUtils::find(serializedOptions);
    if (ShellUtils::startInPresentation(serializedOptions)) {
        doc->startPresentation();
    }
    if (ShellUtils::showPrintDialog(serializedOptions)) {
        QMetaObject::invokeMethod(part, "enableStartWithPrint");
    }
    if (ShellUtils::showPrintDialogAndExit(serializedOptions)) {
        QMetaObject::invokeMethod(part, "enableExitAfterPrint");
    }
    if (!find.isEmpty()) {
        QMetaObject::invokeMethod(part, "enableStartWithFind", Q_ARG(QString, find));
    }
}

void Shell::connectPart(const KParts::ReadWritePart *part)
{
    // NOLINTBEGIN(clazy-old-style-connect);
    // We're abusing the fact we know the part is our part here
    connect(this, SIGNAL(moveSplitter(int)), part, SLOT(moveSplitter(int)));
    connect(part, SIGNAL(enablePrintAction(bool)), this, SLOT(setPrintEnabled(bool)));
    connect(part, SIGNAL(enableCloseAction(bool)), this, SLOT(setCloseEnabled(bool)));
    connect(part, SIGNAL(mimeTypeChanged(QMimeType)), this, SLOT(setTabIcon(QMimeType)));
    connect(part, SIGNAL(urlsDropped(QList<QUrl>)), this, SLOT(handleDroppedUrls(QList<QUrl>)));
    connect(part, SIGNAL(maxRecentItemsChanged(int)), this, SLOT(triggerUpdateRecentItems(int)));

    // clang-format off
    // Formatting disabled to keep signature normalized
    connect(part, SIGNAL(requestOpenNewlySignedFile(QString,int)), this, SLOT(openNewlySignedFile(QString,int)));
    connect(part, SIGNAL(fitWindowToPage(QSize,QSize)), this, SLOT(slotFitWindowToPage(QSize,QSize)));
    connect(part, SIGNAL(aiQueryRequested(QString)), this, SLOT(handleAiQuery(QString)));
    connect(part, SIGNAL(aiAssistantRequested()), this, SLOT(showAiAssistant()));
    // clang-format on
    // NOLINTEND(clazy-old-style-connect);
}

void Shell::print()
{
    QMetaObject::invokeMethod(m_tabs[m_tabWidget->currentIndex()].part, "slotPrint");
}

void Shell::setPrintEnabled(bool enabled)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabs[i].printEnabled = enabled;
        if (i == m_tabWidget->currentIndex()) {
            m_printAction->setEnabled(enabled);
        }
    }
}

void Shell::setCloseEnabled(bool enabled)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabs[i].closeEnabled = enabled;
        if (i == m_tabWidget->currentIndex()) {
            m_closeAction->setEnabled(enabled);
        }
    }
}

void Shell::activateNextTab()
{
    if (m_tabs.size() < 2) {
        return;
    }

    const int activeTab = m_tabWidget->currentIndex();
    const int nextTab = (activeTab == m_tabs.size() - 1) ? 0 : activeTab + 1;

    setActiveTab(nextTab);
}

void Shell::activatePrevTab()
{
    if (m_tabs.size() < 2) {
        return;
    }

    const int activeTab = m_tabWidget->currentIndex();
    const int prevTab = (activeTab == 0) ? m_tabs.size() - 1 : activeTab - 1;

    setActiveTab(prevTab);
}

void Shell::undoCloseTab()
{
    if (m_closedTabUrls.isEmpty()) {
        return;
    }

    const QUrl lastTabUrl = m_closedTabUrls.takeLast();

    if (m_closedTabUrls.isEmpty()) {
        m_undoCloseTab->setEnabled(false);
    }

    openUrl(lastTabUrl);
}

void Shell::setTabIcon(const QMimeType &mimeType)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabWidget->setTabIcon(i, QIcon::fromTheme(mimeType.iconName()));
    }
}

int Shell::findTabIndex(QObject *sender) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].part == sender) {
            return i;
        }
    }
    return -1;
}

int Shell::findTabIndex(const QUrl &url) const
{
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), [&url](const TabState state) { return state.part->url() == url; });
    return (it != m_tabs.end()) ? std::distance(m_tabs.begin(), it) : -1;
}

void Shell::handleDroppedUrls(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        openUrl(url);
    }
}

void Shell::moveTabData(int from, int to)
{
    m_tabs.move(from, to);
}

void Shell::slotFitWindowToPage(const QSize pageViewSize, const QSize pageSize)
{
    const int xOffset = pageViewSize.width() - pageSize.width();
    const int yOffset = pageViewSize.height() - pageSize.height();
    showNormal();
    resize(width() - xOffset, height() - yOffset);
    Q_EMIT moveSplitter(pageSize.width());
}

void Shell::hideWelcomeScreen()
{
    m_centralStackedWidget->setCurrentWidget(m_tabWidget);
    m_sidebar->setVisible(m_showSidebarAction->isChecked());
    m_showSidebarAction->setEnabled(true);
}

void Shell::showWelcomeScreen()
{
    m_showSidebarAction->setEnabled(false);
    m_centralStackedWidget->setCurrentWidget(m_welcomeScreen);
    m_sidebar->setVisible(false);

    refreshRecentsOnWelcomeScreen();
}

void Shell::refreshRecentsOnWelcomeScreen()
{
    saveRecents();
    m_welcomeScreen->loadRecents();
}

void Shell::forgetRecentItem(QUrl const &url)
{
    if (m_recent != nullptr) {
        m_recent->removeUrl(url);
        saveRecents();
        refreshRecentsOnWelcomeScreen();
    }
}

void Shell::triggerUpdateRecentItems(const int maxItems)
{
    m_recent->setMaxItems(maxItems);
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    // saveRecents() dumps the recent files in correct order to KConfigGroup, respecting the allowed no. of recent items, discarding older items if needed
    refreshRecentsOnWelcomeScreen();
    m_recent->loadEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

void Shell::readRecentFilesSettings()
{
    // Read no. of max. recent items from okularpartrc, populate File->Open Recent menu-item as well as recentsListView on welcome screen
    QString configFilePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1Char('/') + QLatin1String("okularpartrc");
    const KConfigGroup confgrp = KSharedConfig::openConfig(configFilePath).data()->group(QStringLiteral("General"));
    const int defaultMaxRecentItems = 10;
    int maxRecentItems = confgrp.readEntry<int>("MaxRecentItems", defaultMaxRecentItems);
    m_recent->setMaxItems(maxRecentItems);
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    m_welcomeScreen->loadRecents();
    m_recent->loadEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

#include "moc_shell.cpp"
#include "shell.moc"

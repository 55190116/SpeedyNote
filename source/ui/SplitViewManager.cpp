// ============================================================================
// SplitViewManager Implementation
// ============================================================================

#include "SplitViewManager.h"
#include "TabBar.h"
#include "TabManager.h"
#include "widgets/ViewportScrollBar.h"
#include "ThumbnailRenderer.h"
#include "../core/DocumentViewport.h"
#include "../core/Document.h"
#include "../core/DarkModeUtils.h"

#include <QStackedWidget>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QPointingDevice>
#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <QInputDevice>
#include <QHash>
#include <QPainter>

// ============================================================================
// Constructor / Destructor
// ============================================================================

SplitViewManager::SplitViewManager(QWidget* parent)
    : QWidget(parent)
{
    // --- Tab bar container (horizontal row of tab bars) ---
    m_tabBarContainer = new QWidget(this);
    m_tabBarLayout = new QHBoxLayout(m_tabBarContainer);
    m_tabBarLayout->setContentsMargins(0, 0, 0, 0);
    m_tabBarLayout->setSpacing(0);

    // --- Viewport splitter ---
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(3);

    // Sync tab bar widths with splitter proportions
    connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
        if (!isSplit() || !m_rightTabBar) return;
        QList<int> sizes = m_splitter->sizes();
        if (sizes.size() >= 2 && sizes[0] + sizes[1] > 0) {
            m_tabBarLayout->setStretch(0, sizes[0]);
            m_tabBarLayout->setStretch(1, sizes[1]);
        }
    });

    // --- Create left pane (always exists) ---
    m_leftTabBar = new TabBar(m_tabBarContainer);
    m_leftViewportStack = new QStackedWidget(m_splitter);
    m_leftViewportStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_leftViewportStack->setMinimumWidth(200);
    m_leftTabManager = new TabManager(m_leftTabBar, m_leftViewportStack, this);

    m_tabBarLayout->addWidget(m_leftTabBar, 1);
    m_splitter->addWidget(m_leftViewportStack);

    // Connect left pane signals
    connect(m_leftTabManager, &TabManager::currentViewportChanged,
            this, &SplitViewManager::onLeftViewportChanged);
    connect(m_leftTabManager, &TabManager::tabCloseAttempted,
            this, &SplitViewManager::onLeftTabCloseAttempted);
    connect(m_leftTabManager, &TabManager::tabCloseRequested,
            this, &SplitViewManager::onLeftTabCloseRequested);

    // Tab context menu: split/merge
    connect(m_leftTabBar, &TabBar::splitRequested, this, [this](int index) {
        splitTab(index, Left);
    });
    connect(m_leftTabBar, &TabBar::mergeAllRequested, this, [this]() {
        mergePanes();
    });

    // Forward left-pane tab count changes to the unified totalTabCountChanged signal.
    connect(m_leftTabBar, &TabBar::tabCountChanged, this, [this](int) {
        emit totalTabCountChanged(totalTabCount());
    });

    // Application-level event filter catches mouse/tablet/touch on ANY
    // descendant widget (viewports, tab bars, etc.) for pane activation
    // and scroll-bar proximity/reposition (SB1).
    QApplication::instance()->installEventFilter(this);

    // No right pane initially
    m_activePane = Left;

    // Enhanced scroll bars (SB1): read the persisted pin state, then create
    // the always-present left pane's overlay bars.
    {
        QSettings settings;
        m_scrollBarsPinned = settings.value(QStringLiteral("scrollbar/pinned"),
                                             defaultScrollBarsPinned()).toBool();
    }

    // SB3: debounce timer that rebuilds any pane's thumbnail strip once its track
    // size settles after a resize.
    m_stripResizeTimer = new QTimer(this);
    m_stripResizeTimer->setSingleShot(true);
    m_stripResizeTimer->setInterval(150);
    connect(m_stripResizeTimer, &QTimer::timeout, this, [this]() {
        for (int i = 0; i < 2; ++i) {
            PaneBars& b = m_paneBars[i];
            if (b.vBar && b.bound && b.vBar->trackContentPixelSize() != b.stripPxSize) {
                rebuildThumbnailStrip(static_cast<Pane>(i));
            }
        }
    });

    createScrollBars(Left);
}

SplitViewManager::~SplitViewManager()
{
    // TabManagers are children of this, so Qt handles deletion
}

// ============================================================================
// Active Pane
// ============================================================================

SplitViewManager::Pane SplitViewManager::activePane() const
{
    return m_activePane;
}

void SplitViewManager::setActivePane(Pane pane)
{
    if (pane == Right && !m_rightTabManager)
        return;

    if (m_activePane != pane) {
        m_activePane = pane;
        updateActivePaneIndicator();
        emit activePaneChanged(pane);
        DocumentViewport* vp = activeViewport();
        emit activeViewportChanged(vp);
    }
}

DocumentViewport* SplitViewManager::activeViewport() const
{
    TabManager* tm = activeTabManager();
    return tm ? tm->currentViewport() : nullptr;
}

DocumentViewport* SplitViewManager::inactiveViewport() const
{
    if (!isSplit()) return nullptr;
    TabManager* tm = (m_activePane == Left) ? m_rightTabManager : m_leftTabManager;
    return tm ? tm->currentViewport() : nullptr;
}

// ============================================================================
// Split Control
// ============================================================================

bool SplitViewManager::isSplit() const
{
    return m_rightTabManager != nullptr;
}

void SplitViewManager::splitTab(int tabIndex, Pane sourcePane)
{
    TabManager* source = (sourcePane == Left) ? m_leftTabManager : m_rightTabManager;
    if (!source || tabIndex < 0 || tabIndex >= source->tabCount())
        return;

    // Don't split if it's the only tab in the only pane
    if (source->tabCount() <= 1 && !isSplit())
        return;

    // Don't move if source has only 1 tab and it's the left pane while split
    // (moving it right would empty the left; the auto-merge handles right→empty)
    if (source->tabCount() <= 1 && sourcePane == Left && isSplit())
        return;

    // Create right pane if needed
    if (!isSplit()) {
        createRightPane();
    }

    TabManager* target = (sourcePane == Left) ? m_rightTabManager : m_leftTabManager;

    // Detach viewport from source, attach to target (preserving state)
    TabManager::DetachedTab tab = source->detachTab(tabIndex);
    if (!tab.viewport)
        return;

    target->attachTab(tab.viewport, tab.title, tab.modified, tab.tabId);

    // If source pane (right) is now empty, auto-merge
    if (m_rightTabManager && m_rightTabManager->tabCount() == 0) {
        destroyRightPane();
    }

    // Activate the target pane
    Pane targetPane = (sourcePane == Left) ? Right : Left;
    setActivePane(targetPane);

    // Recenter all visible viewports after geometry settles
    recenterAllViewports();
}

void SplitViewManager::mergePanes()
{
    if (!isSplit())
        return;

    // Move all tabs from right to left (preserving state)
    while (m_rightTabManager->tabCount() > 0) {
        TabManager::DetachedTab tab = m_rightTabManager->detachTab(0);
        if (!tab.viewport) break;

        m_leftTabManager->attachTab(tab.viewport, tab.title, tab.modified, tab.tabId);
    }

    destroyRightPane();
    setActivePane(Left);

    // Recenter after merging back to single pane
    recenterAllViewports();
}

// ============================================================================
// Delegated TabManager API
// ============================================================================

int SplitViewManager::createTab(Document* doc, const QString& title)
{
    return createTabInPane(doc, title, m_activePane);
}

int SplitViewManager::createTabInPane(Document* doc, const QString& title, Pane pane)
{
    TabManager* tm = (pane == Left) ? m_leftTabManager : m_rightTabManager;
    if (!tm) tm = m_leftTabManager;
    return tm->createTab(doc, title);
}

int SplitViewManager::totalTabCount() const
{
    int count = m_leftTabManager ? m_leftTabManager->tabCount() : 0;
    if (m_rightTabManager) count += m_rightTabManager->tabCount();
    return count;
}

int SplitViewManager::activeTabCount() const
{
    TabManager* tm = activeTabManager();
    return tm ? tm->tabCount() : 0;
}

TabManager* SplitViewManager::activeTabManager() const
{
    if (m_activePane == Right && m_rightTabManager)
        return m_rightTabManager;
    return m_leftTabManager;
}

TabManager* SplitViewManager::leftTabManager() const { return m_leftTabManager; }
TabManager* SplitViewManager::rightTabManager() const { return m_rightTabManager; }
TabBar* SplitViewManager::leftTabBar() const { return m_leftTabBar; }
TabBar* SplitViewManager::rightTabBar() const { return m_rightTabBar; }
QStackedWidget* SplitViewManager::leftViewportStack() const { return m_leftViewportStack; }
QStackedWidget* SplitViewManager::rightViewportStack() const { return m_rightViewportStack; }

// ============================================================================
// Iteration
// ============================================================================

QVector<SplitViewManager::TabRef> SplitViewManager::allTabs() const
{
    QVector<TabRef> refs;
    if (m_leftTabManager) {
        for (int i = 0; i < m_leftTabManager->tabCount(); ++i)
            refs.append({Left, i});
    }
    if (m_rightTabManager) {
        for (int i = 0; i < m_rightTabManager->tabCount(); ++i)
            refs.append({Right, i});
    }
    return refs;
}

void SplitViewManager::updateTheme(bool darkMode, const QColor& accentColor)
{
    m_darkMode = darkMode;
    m_accentColor = accentColor;
    if (m_leftTabBar) m_leftTabBar->updateTheme(darkMode, accentColor);
    if (m_rightTabBar) m_rightTabBar->updateTheme(darkMode, accentColor);
    applyScrollBarDarkMode();
    updateActivePaneIndicator();
}

QWidget* SplitViewManager::tabBarContainer() const { return m_tabBarContainer; }
QSplitter* SplitViewManager::viewportSplitter() const { return m_splitter; }

// ============================================================================
// Signal Handlers
// ============================================================================

void SplitViewManager::onLeftViewportChanged(DocumentViewport* vp)
{
    bindScrollBars(Left, vp);
    if (m_activePane == Left) {
        emit activeViewportChanged(vp);
    }
}

void SplitViewManager::onRightViewportChanged(DocumentViewport* vp)
{
    bindScrollBars(Right, vp);
    if (m_activePane == Right) {
        emit activeViewportChanged(vp);
    }
}

void SplitViewManager::onLeftTabCloseAttempted(int index, DocumentViewport* vp)
{
    int tabId = m_leftTabManager->tabIdAt(index);
    emit tabCloseAttempted(tabId, vp, Left);
}

void SplitViewManager::onRightTabCloseAttempted(int index, DocumentViewport* vp)
{
    if (!m_rightTabManager) return;
    int tabId = m_rightTabManager->tabIdAt(index);
    emit tabCloseAttempted(tabId, vp, Right);
}

void SplitViewManager::onLeftTabCloseRequested(int index, DocumentViewport* vp)
{
    int tabId = m_leftTabManager->tabIdAt(index);
    emit tabCloseRequested(tabId, vp, Left);
}

void SplitViewManager::onRightTabCloseRequested(int index, DocumentViewport* vp)
{
    if (!m_rightTabManager) return;
    int tabId = m_rightTabManager->tabIdAt(index);
    emit tabCloseRequested(tabId, vp, Right);
}

// ============================================================================
// Private: Pane Management
// ============================================================================

void SplitViewManager::createRightPane()
{
    if (m_rightTabManager)
        return;

    m_rightTabBar = new TabBar(m_tabBarContainer);
    m_rightViewportStack = new QStackedWidget(m_splitter);
    m_rightViewportStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightViewportStack->setMinimumWidth(200);
    m_rightTabManager = new TabManager(m_rightTabBar, m_rightViewportStack, this);

    m_splitter->addWidget(m_rightViewportStack);

    // Even split
    m_splitter->setSizes({1, 1});

    updateTabBarContainerLayout();

    // Connect right pane signals
    connect(m_rightTabManager, &TabManager::currentViewportChanged,
            this, &SplitViewManager::onRightViewportChanged);
    connect(m_rightTabManager, &TabManager::tabCloseAttempted,
            this, &SplitViewManager::onRightTabCloseAttempted);
    connect(m_rightTabManager, &TabManager::tabCloseRequested,
            this, &SplitViewManager::onRightTabCloseRequested);

    // Tab context menu: split/merge
    connect(m_rightTabBar, &TabBar::splitRequested, this, [this](int index) {
        splitTab(index, Right);
    });
    connect(m_rightTabBar, &TabBar::mergeAllRequested, this, [this]() {
        mergePanes();
    });

    // Forward right-pane tab count changes to the unified totalTabCountChanged signal.
    connect(m_rightTabBar, &TabBar::tabCountChanged, this, [this](int) {
        emit totalTabCountChanged(totalTabCount());
    });

    // Apply cached theme to new tab bar
    if (m_accentColor.isValid())
        m_rightTabBar->updateTheme(m_darkMode, m_accentColor);

    // Update context menu state
    m_leftTabBar->setMergeEnabled(true);
    m_rightTabBar->setMergeEnabled(true);

    // SB1: give the new pane its own overlay scroll bars, bound to its viewport.
    createScrollBars(Right);

    updateActivePaneIndicator();
    emit splitStateChanged(true);
}

void SplitViewManager::destroyRightPane()
{
    if (!m_rightTabManager)
        return;

    if (m_activePane == Right)
        m_activePane = Left;

    // SB1: tear down the right pane's overlay bars before the stack is deleted.
    destroyScrollBars(Right);

    // Disconnect all signals so no stale emissions occur
    disconnect(m_rightTabManager, nullptr, this, nullptr);
    disconnect(m_rightTabBar, nullptr, this, nullptr);

    // Hide immediately so they disappear from the UI
    m_rightTabBar->hide();
    m_rightViewportStack->hide();

    // Use deleteLater() instead of delete -- the context menu action
    // that triggered mergePanes() may still be on the call stack inside
    // TabBar::contextMenuEvent, so destroying the TabBar synchronously
    // would be a use-after-free.
    m_rightTabManager->deleteLater();
    m_rightTabBar->deleteLater();
    m_rightViewportStack->deleteLater();

    m_rightTabManager = nullptr;
    m_rightTabBar = nullptr;
    m_rightViewportStack = nullptr;

    updateTabBarContainerLayout();

    // Update context menu state
    m_leftTabBar->setMergeEnabled(false);

    updateActivePaneIndicator();
    emit splitStateChanged(false);

    // Title refresh: setActivePane() short-circuits when the new pane equals
    // the old (we set m_activePane=Left above), so subscribers like the
    // navigation bar would otherwise miss the post-merge viewport switch
    // (the last activeViewportChanged could be nullptr from the right pane
    // emptying while it was still the active pane).
    emit activeViewportChanged(activeViewport());

    // Tab-bar autohide refresh: when the right pane is destroyed with 0 tabs
    // (e.g., user closed the last right tab while left has only 1 tab), the
    // surviving left TabBar's count never changed so it emits no tabCountChanged,
    // and the right TabBar's deferred tabCountChanged is dropped because we
    // disconnected it above. Without this explicit emit, MainWindow's autohide
    // handler would never re-evaluate after the merge.
    emit totalTabCountChanged(totalTabCount());
}

void SplitViewManager::updateTabBarContainerLayout()
{
    // Remove all items from layout (without deleting the widgets themselves)
    while (m_tabBarLayout->count() > 0) {
        delete m_tabBarLayout->takeAt(0);
    }

    m_tabBarLayout->addWidget(m_leftTabBar, 1);
    m_leftTabBar->show();

    if (m_rightTabBar) {
        m_tabBarLayout->addWidget(m_rightTabBar, 1);
        m_rightTabBar->show();
    }
}

void SplitViewManager::updateActivePaneIndicator()
{
    if (!isSplit()) {
        m_leftViewportStack->setStyleSheet(QString());
        return;
    }

    QString color = m_accentColor.isValid() ? m_accentColor.name()
                                            : QStringLiteral("palette(highlight)");
    QString activeStyle = QStringLiteral(
        "QStackedWidget { border-top: 2px solid %1; }").arg(color);
    static const QString inactiveStyle = QStringLiteral(
        "QStackedWidget { border-top: 2px solid transparent; }");

    m_leftViewportStack->setStyleSheet(
        m_activePane == Left ? activeStyle : inactiveStyle);
    if (m_rightViewportStack) {
        m_rightViewportStack->setStyleSheet(
            m_activePane == Right ? activeStyle : inactiveStyle);
    }
}

// ============================================================================
// Recenter viewports after layout change (split/merge)
// ============================================================================

void SplitViewManager::recenterAllViewports()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_leftTabManager) {
            if (DocumentViewport* vp = m_leftTabManager->currentViewport())
                vp->zoomToWidth();
        }
        if (m_rightTabManager) {
            if (DocumentViewport* vp = m_rightTabManager->currentViewport())
                vp->zoomToWidth();
        }
    });
}

// ============================================================================
// Event Filter (pane activation on any interaction)
// ============================================================================

bool SplitViewManager::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();

    // SB1: keep overlay bars laid out when a pane stack resizes or is shown.
    if (type == QEvent::Resize || type == QEvent::Show) {
        if (watched == m_leftViewportStack) {
            repositionScrollBars(Left);
        } else if (m_rightViewportStack && watched == m_rightViewportStack) {
            repositionScrollBars(Right);
        }
    }

    // SB1: pen/mouse proximity floats the bars in (palm-rejected inside).
    if (type == QEvent::MouseMove || type == QEvent::TabletMove) {
        proximityFloatCheck(event);
    }

    // Pane activation on any interaction (only meaningful when split).
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::TabletPress:
    case QEvent::TouchBegin:
        if (isSplit()) {
            if (QWidget* target = qobject_cast<QWidget*>(watched)) {
                // Walk up the parent chain to find the owning pane.
                for (QWidget* w = target; w != nullptr; w = w->parentWidget()) {
                    if (w == m_leftViewportStack || w == m_leftTabBar) {
                        setActivePane(Left);
                        break;
                    }
                    if (w == m_rightViewportStack || w == m_rightTabBar) {
                        setActivePane(Right);
                        break;
                    }
                }
            }
        }
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

// ============================================================================
// Enhanced scroll bars (Plan SB1)
// ============================================================================

bool SplitViewManager::defaultScrollBarsPinned()
{
    // Default to pinned (always visible) when a physical keyboard is present,
    // matching the pre-SB1 keyboard-keyed visibility behavior.
    const auto devices = QInputDevice::devices();
    for (const QInputDevice* device : devices) {
        if (device && device->type() == QInputDevice::DeviceType::Keyboard) {
            return true;
        }
    }
    return false;
}

QStackedWidget* SplitViewManager::stackForPane(Pane pane) const
{
    return (pane == Left) ? m_leftViewportStack : m_rightViewportStack;
}

DocumentViewport* SplitViewManager::viewportForPane(Pane pane) const
{
    TabManager* tm = (pane == Left) ? m_leftTabManager : m_rightTabManager;
    return tm ? tm->currentViewport() : nullptr;
}

void SplitViewManager::createScrollBars(Pane pane)
{
    QStackedWidget* stack = stackForPane(pane);
    if (!stack) return;

    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (b.vBar) return;  // already created

    b.vBar = new ViewportScrollBar(Qt::Vertical, ViewportScrollBar::DockEdge::Left, stack);
    b.hBar = new ViewportScrollBar(Qt::Horizontal, ViewportScrollBar::DockEdge::Top, stack);
    b.vBar->setDarkMode(m_darkMode);
    b.hBar->setDarkMode(m_darkMode);

    b.fadeTimer = new QTimer(this);
    b.fadeTimer->setSingleShot(true);
    b.fadeTimer->setInterval(2500);  // ~2.5s of inactivity before fade-out
    connect(b.fadeTimer, &QTimer::timeout, this, [this, pane]() {
        hideScrollBars(pane);
    });

    // SB3: per-pane async thumbnail renderer + single-slice edit debounce.
    b.thumbRenderer = new ThumbnailRenderer(this);
    b.thumbRenderer->setMaxConcurrentRenders(2);
    b.cThumbReady = connect(b.thumbRenderer, &ThumbnailRenderer::thumbnailReady, this,
                            [this, pane](int pageIndex, QPixmap pixmap) {
        compositeThumbnailSlice(pane, pageIndex, pixmap);
    });
    b.stripEditTimer = new QTimer(this);
    b.stripEditTimer->setSingleShot(true);
    b.stripEditTimer->setInterval(400);  // "between intra-page actions"
    connect(b.stripEditTimer, &QTimer::timeout, this, [this, pane]() {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.pendingEditPage >= 0) refreshThumbnailSlice(pane, pb.pendingEditPage);
    });

    // Initial visibility follows the pin state.
    b.vBar->setVisible(m_scrollBarsPinned);
    b.hBar->setVisible(m_scrollBarsPinned);

    repositionScrollBars(pane);
    bindScrollBars(pane, viewportForPane(pane));
}

void SplitViewManager::destroyScrollBars(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    disconnect(b.cViewToV);
    disconnect(b.cViewToH);
    disconnect(b.cVToView);
    disconnect(b.cHToView);
    disconnect(b.cMarker);
    disconnect(b.cThumbReady);
    disconnect(b.cPageModified);
    if (b.fadeTimer) { b.fadeTimer->stop(); delete b.fadeTimer; }
    if (b.stripEditTimer) { b.stripEditTimer->stop(); delete b.stripEditTimer; }
    if (b.thumbRenderer) { b.thumbRenderer->cancelAll(); delete b.thumbRenderer; }
    delete b.vBar;
    delete b.hBar;
    b = PaneBars{};
}

void SplitViewManager::repositionScrollBars(Pane pane)
{
    QStackedWidget* stack = stackForPane(pane);
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!stack || !b.vBar || !b.hBar) return;

    const int vThickness = ViewportScrollBar::barThickness(Qt::Vertical);
    const int hThickness = ViewportScrollBar::barThickness(Qt::Horizontal);
    const int margin = 3;
    const int w = stack->width();
    const int h = stack->height();

    // Vertical (page-axis) bar on the LEFT edge; horizontal (cross-axis) on TOP.
    // Each bar's start offset clears the perpendicular bar's thickness so the
    // wider vertical bar never overlaps the horizontal one at the corner.
    const int vTop = hThickness + margin * 2;   // vBar starts below the hBar
    const int hLeft = vThickness + margin * 2;  // hBar starts right of the vBar
    b.vBar->setGeometry(margin,
                        vTop,
                        vThickness,
                        qMax(0, h - vTop - margin));
    b.hBar->setGeometry(hLeft,
                        margin,
                        qMax(0, w - hLeft - margin),
                        hThickness);
    b.vBar->raise();
    b.hBar->raise();

    // SB3: build (first valid layout) or re-lay (size changed) the strip;
    // coalesce rapid resize ticks into a single rebuild once the size settles.
    // stripPxSize is empty until the first successful build, so this also drives
    // the initial build once the bar finally has a real track size.
    if (b.bound && m_stripResizeTimer) {
        const QSize trackPx = b.vBar->trackContentPixelSize();
        if (!trackPx.isEmpty() && trackPx != b.stripPxSize) {
            m_stripResizeTimer->start();
        }
    }
}

void SplitViewManager::bindScrollBars(Pane pane, DocumentViewport* vp)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.vBar || !b.hBar) return;

    // Drop connections to the previous viewport.
    disconnect(b.cViewToV);
    disconnect(b.cViewToH);
    disconnect(b.cVToView);
    disconnect(b.cHToView);
    disconnect(b.cMarker);
    disconnect(b.cPageModified);
    b.cViewToV = b.cViewToH = b.cVToView = b.cHToView = b.cMarker = QMetaObject::Connection{};
    b.cPageModified = QMetaObject::Connection{};

    // SB3: dropping the old viewport invalidates any in-flight strip work and
    // frees the composited strip so it never lingers for a closed document.
    if (b.thumbRenderer) b.thumbRenderer->cancelAll();
    b.stripQueue.clear();
    b.stripInFlight.clear();
    b.stripSlots.clear();
    b.pendingEditPage = -1;
    if (b.stripEditTimer) b.stripEditTimer->stop();
    b.strip = QPixmap();
    b.stripPxSize = QSize();
    if (b.vBar) b.vBar->setThumbnailStrip(QPixmap());

    b.bound = vp;
    if (!vp) return;

    // Initialize handle sizes and positions from the viewport's current state.
    refreshHandleSizes(pane);
    {
        qreal zoom = vp->zoomLevel();
        if (zoom <= 0) zoom = 1.0;
        const QPointF panOffset = vp->panOffset();
        const QSizeF content = vp->totalContentSize();
        const qreal viewW = vp->width() / zoom;
        const qreal viewH = vp->height() / zoom;
        const qreal scrollW = content.width() - viewW;
        const qreal scrollH = content.height() - viewH;
        b.vBar->setFraction(scrollH > 0 ? qBound(0.0, panOffset.y() / scrollH, 1.0) : 0.0);
        b.hBar->setFraction(scrollW > 0 ? qBound(0.0, panOffset.x() / scrollW, 1.0) : 0.0);
    }

    // Viewport -> bar (programmatic; does not feed back).
    b.cViewToV = connect(vp, &DocumentViewport::verticalScrollChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (!pb.vBar) return;
        refreshHandleSizes(pane);
        pb.vBar->setFraction(f);
        showScrollBars(pane);  // float in during active scroll
    });
    b.cViewToH = connect(vp, &DocumentViewport::horizontalScrollChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (!pb.hBar) return;
        refreshHandleSizes(pane);
        pb.hBar->setFraction(f);
        showScrollBars(pane);
    });

    // Bar -> viewport (user interaction only).
    b.cVToView = connect(b.vBar, &ViewportScrollBar::fractionChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.bound) pb.bound->setVerticalScrollFraction(f);
    });
    b.cHToView = connect(b.hBar, &ViewportScrollBar::fractionChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.bound) pb.bound->setHorizontalScrollFraction(f);
    });

    // SB2: clicking a link marker jumps the bound viewport to that page.
    b.cMarker = connect(b.vBar, &ViewportScrollBar::markerActivated, this,
                        [this, pane](int pageIndex) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.bound && pageIndex >= 0) pb.bound->scrollToPage(pageIndex);
    });

    // SB2: compute the per-source accent + link-marker document map now.
    updateScrollBarDocumentMap(vp);

    // SB3: a visible-page edit refreshes only that page's slice (debounced),
    // so the strip stays cheap during drawing.
    b.cPageModified = connect(vp, &DocumentViewport::pageModified, this,
                              [this, pane](int pageIndex) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pageIndex < 0 || !pb.stripEditTimer) return;
        pb.pendingEditPage = pageIndex;
        pb.stripEditTimer->start();
    });

    // SB3: build the low-res thumbnail strip for the new viewport.
    rebuildThumbnailStrip(pane);

    // Keep the bars above the (possibly newly shown) viewport.
    b.vBar->raise();
    b.hBar->raise();
}

void SplitViewManager::updateScrollBarDocumentMap(DocumentViewport* vp)
{
    if (!vp) return;

    // Find the pane currently bound to this viewport.
    int paneIdx = -1;
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].bound == vp) { paneIdx = i; break; }
    }
    if (paneIdx < 0) return;

    PaneBars& b = m_paneBars[paneIdx];
    if (!b.vBar) return;

    Document* doc = vp->document();
    if (!doc) {
        b.vBar->setAccentRegions({});
        b.vBar->setMarkers({});
        return;
    }

    // --- Per-source accent bands ---------------------------------------
    // Single-source (or plain) documents get no stripes: parity with SB1.
    QVector<ViewportScrollBar::AccentRegion> accents;
    const QStringList order = doc->sourceDisplayOrder();
    if (order.size() > 1) {
        // The palette slot IS the index in sourceDisplayOrder(); precompute a
        // lookup so the per-page loop stays O(pages) instead of calling
        // paletteSlotForSource() (which rebuilds the order list every call).
        QHash<QString, int> slotOf;
        slotOf.reserve(order.size());
        for (int i = 0; i < order.size(); ++i) slotOf.insert(order[i], i);

        const int pageCount = doc->pageCount();
        int runStart = -1;
        int runSlot = -2;  // -2 = no active run
        auto flushRun = [&](int runEnd) {
            if (runSlot < 0 || runStart < 0) return;  // plain-page run: no stripe
            const qreal start = vp->pageTrackFraction(runStart);
            const qreal end = vp->pageTrackFraction(runEnd + 1);  // bottom of last page
            if (start >= 0.0 && end > start) {
                const QColor c = DarkModeUtils::sourceAccentColor(runSlot, m_darkMode);
                if (c.isValid()) accents.push_back({ start, end, c });
            }
        };
        for (int i = 0; i < pageCount; ++i) {
            QString srcId;
            int pdfPage = -1;
            int slot = -1;  // plain page
            if (doc->pdfBindingForNotebookPage(i, srcId, pdfPage)) {
                slot = slotOf.value(srcId, -1);
            }
            if (slot != runSlot) {
                flushRun(i - 1);
                runSlot = slot;
                runStart = i;
            }
        }
        flushRun(pageCount - 1);
    }
    b.vBar->setAccentRegions(accents);

    // --- Link markers ---------------------------------------------------
    QVector<ViewportScrollBar::BarMarker> markers;
    const QVector<Document::PageLinkMarker> pageMarkers = doc->pageLinkMarkers();
    markers.reserve(pageMarkers.size());
    for (const Document::PageLinkMarker& pm : pageMarkers) {
        const qreal frac = vp->pageTrackFraction(pm.pageIndex);
        if (frac < 0.0) continue;
        ViewportScrollBar::BarMarker m;
        m.pos = frac;
        m.color = pm.color;
        m.pageIndex = pm.pageIndex;
        m.kind = ViewportScrollBar::MarkerKind::Link;
        m.tooltip = pm.description;
        markers.push_back(std::move(m));
    }
    b.vBar->setMarkers(markers);
}

// ============================================================================
// SB3: low-res thumbnail strip
// ============================================================================
//
// Memory model: there is exactly ONE composited strip pixmap per pane, sized to
// the bar's track in device pixels (a few hundred KB, independent of page
// count). Per-page thumbnails are rendered tiny, painted into the strip, and
// discarded -- we never keep a per-page pixmap cache. Render count is bounded by
// band-sampling the track (one representative page per ~6px band), so even a
// multi-thousand-page PDF triggers only a few hundred tiny async renders.

void SplitViewManager::rebuildScrollBarThumbnails(DocumentViewport* vp)
{
    if (!vp) return;
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].bound == vp) { rebuildThumbnailStrip(static_cast<Pane>(i)); return; }
    }
}

void SplitViewManager::rebuildThumbnailStrip(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.vBar || !b.thumbRenderer) return;

    DocumentViewport* vp = b.bound;
    Document* doc = vp ? vp->document() : nullptr;

    // Free the strip and cancel work when there's nothing to show (this also
    // covers edgeless documents, which have no page track).
    auto clearStrip = [&]() {
        b.thumbRenderer->cancelAll();
        b.stripQueue.clear();
        b.stripInFlight.clear();
        b.stripSlots.clear();
        b.strip = QPixmap();
        b.stripPxSize = QSize();
        b.vBar->setThumbnailStrip(QPixmap());
    };
    if (!doc || doc->pageCount() <= 0 || doc->isEdgeless()) { clearStrip(); return; }

    const QSize pxSize = b.vBar->trackContentPixelSize();
    if (pxSize.isEmpty()) { clearStrip(); return; }  // bar not laid out yet

    // Barrier: cancelAll() cancels + waits, so no stale thumbnailReady survives
    // to paint onto a page whose identity changed (e.g. after a reorder).
    b.thumbRenderer->cancelAll();
    b.stripQueue.clear();
    b.stripInFlight.clear();

    // PDF inversion must match the on-screen viewport.
    b.thumbRenderer->setPdfDarkMode(vp->isDarkMode() && vp->isPdfDarkModeEnabled());

    // Allocate the single composited strip in raw device pixels (no DPR tag: the
    // bar scales it into the logical track rect). Transparent base so blank
    // track (few-page docs / letterbox) shows the real track background through.
    QPixmap strip(pxSize);
    strip.fill(Qt::transparent);
    b.strip = strip;
    b.stripPxSize = pxSize;
    b.stripSlots.clear();

    // Filmstrip layout: aspect-correct thumbnails evenly spread across the bar.
    // Slot height comes from a representative page aspect so each thumbnail is
    // readable rather than squished into a 1px scroll-position slice. maxFit caps
    // how many pages we show so slivers never become unreadable.
    const int stripW = pxSize.width();
    const int stripH = pxSize.height();
    const int pageCount = doc->pageCount();

    QSizeF p0 = doc->pageSizeAt(0);
    if (p0.width() <= 0.0 || p0.height() <= 0.0) p0 = QSizeF(612, 792);  // US Letter
    const qreal aspect = p0.height() / p0.width();
    const int slotH = qMax(1, qRound(stripW * aspect));

    const int maxFit = qMax(1, stripH / slotH);
    const int shownCount = qMin(pageCount, maxFit);

    // Spread the shown pages evenly across the FULL bar height in equal bands.
    // Each thumbnail is drawn at its natural aspect ratio and centered in its
    // band (see compositeThumbnailSlice), so few-page docs read as "page, blank,
    // page, blank..." with the solid bar background as the filler, while
    // full docs pack edge-to-edge (band ~= natural thumbnail height).
    for (int k = 0; k < shownCount; ++k) {
        // Evenly sample across the whole document (k=0 -> first, last -> last).
        int page = static_cast<int>((static_cast<qint64>(k) * pageCount) / shownCount
                                    + pageCount / (2 * shownCount));
        page = qBound(0, page, pageCount - 1);
        // Distinct pages only (guard against collisions when shownCount ~ pageCount).
        if (b.stripSlots.contains(page)) continue;
        const int y0 = static_cast<int>((static_cast<qint64>(k) * stripH) / shownCount);
        const int y1 = static_cast<int>((static_cast<qint64>(k + 1) * stripH) / shownCount);
        b.stripSlots.insert(page, QRect(0, y0, stripW, qMax(1, y1 - y0)));
        b.stripQueue.append(page);
    }

    // Show the (transparent) strip immediately (accents/handle/markers already
    // paint); thumbnails fill in asynchronously.
    b.vBar->setThumbnailStrip(b.strip);
    feedThumbnailQueue(pane);
}

void SplitViewManager::feedThumbnailQueue(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.thumbRenderer || !b.vBar) return;
    DocumentViewport* vp = b.bound;
    Document* doc = vp ? vp->document() : nullptr;
    if (!doc || b.stripPxSize.isEmpty()) return;

    const qreal dpr = b.vBar->devicePixelRatioF();
    const int wLogical = qMax(1, qRound(b.stripPxSize.width() / dpr));

    // Keep at most kMaxInFlight requests outstanding. The renderer runs 2
    // concurrently and drops nothing below its 8-deep queue, so 6 never overflows.
    constexpr int kMaxInFlight = 6;
    while (b.stripInFlight.size() < kMaxInFlight && !b.stripQueue.isEmpty()) {
        const int page = b.stripQueue.takeFirst();
        if (page < 0 || page >= doc->pageCount() || b.stripInFlight.contains(page)) continue;
        b.stripInFlight.insert(page);
        b.thumbRenderer->requestThumbnail(doc, page, wLogical, dpr);
    }
}

void SplitViewManager::compositeThumbnailSlice(Pane pane, int page, const QPixmap& pixmap)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    b.stripInFlight.remove(page);

    // Only pages that are part of the current filmstrip have a slot; ignore
    // stale results for pages no longer shown.
    if (b.vBar && !b.strip.isNull() && !pixmap.isNull() && b.stripSlots.contains(page)) {
        const QRect slot = b.stripSlots.value(page);
        // Scale the thumbnail to fit the slot preserving aspect ratio, centered
        // (letterbox rather than distort), so mixed page sizes stay undistorted.
        const QSize scaled = pixmap.size().scaled(slot.size(), Qt::KeepAspectRatio);
        const int dx = slot.x() + (slot.width() - scaled.width()) / 2;
        const int dy = slot.y() + (slot.height() - scaled.height()) / 2;

        QPainter p(&b.strip);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // Clear the slot first so a single-page refresh overwrites cleanly.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(slot, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.drawPixmap(QRect(dx, dy, scaled.width(), scaled.height()), pixmap);
        p.end();
        b.vBar->setThumbnailStrip(b.strip);
    }
    // pixmap is a local copy and goes out of scope here -- no per-page cache.
    feedThumbnailQueue(pane);
}

void SplitViewManager::refreshThumbnailSlice(Pane pane, int page)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.thumbRenderer || !b.vBar || b.strip.isNull() || b.stripPxSize.isEmpty()) return;
    DocumentViewport* vp = b.bound;
    Document* doc = vp ? vp->document() : nullptr;
    if (!doc || page < 0 || page >= doc->pageCount()) return;
    // Only refresh pages that are actually part of the current filmstrip.
    if (!b.stripSlots.contains(page)) return;
    if (b.stripInFlight.contains(page)) return;  // already being rendered

    const qreal dpr = b.vBar->devicePixelRatioF();
    const int wLogical = qMax(1, qRound(b.stripPxSize.width() / dpr));
    b.stripInFlight.insert(page);
    b.thumbRenderer->requestThumbnail(doc, page, wLogical, dpr);
}

void SplitViewManager::refreshHandleSizes(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    DocumentViewport* vp = b.bound;
    if (!vp || !b.vBar || !b.hBar) return;

    qreal zoom = vp->zoomLevel();
    if (zoom <= 0) zoom = 1.0;
    const QSizeF content = vp->totalContentSize();
    if (content.width() <= 0 || content.height() <= 0) return;

    const qreal viewW = vp->width() / zoom;
    const qreal viewH = vp->height() / zoom;
    b.vBar->setHandleFraction(qBound(0.0, viewH / content.height(), 1.0));
    b.hBar->setHandleFraction(qBound(0.0, viewW / content.width(), 1.0));
}

void SplitViewManager::showScrollBars(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.vBar || !b.hBar) return;

    if (!b.vBar->isVisible()) { b.vBar->setVisible(true); b.vBar->raise(); }
    if (!b.hBar->isVisible()) { b.hBar->setVisible(true); b.hBar->raise(); }

    // When pinned, the bars stay up; otherwise (re)start the fade timer.
    if (m_scrollBarsPinned) {
        if (b.fadeTimer) b.fadeTimer->stop();
    } else if (b.fadeTimer) {
        b.fadeTimer->start();
    }
}

void SplitViewManager::hideScrollBars(Pane pane)
{
    if (m_scrollBarsPinned) return;
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    // Never hide while the user is actively dragging a handle.
    if (b.vBar && b.vBar->isDragging()) return;
    if (b.hBar && b.hBar->isDragging()) return;
    if (b.vBar) b.vBar->setVisible(false);
    if (b.hBar) b.hBar->setVisible(false);
}

void SplitViewManager::applyScrollBarDarkMode()
{
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].vBar) m_paneBars[i].vBar->setDarkMode(m_darkMode);
        if (m_paneBars[i].hBar) m_paneBars[i].hBar->setDarkMode(m_darkMode);
        // SB2 accent colors are theme-dependent, so recompute the document map.
        if (m_paneBars[i].bound) updateScrollBarDocumentMap(m_paneBars[i].bound);
        // SB3 thumbnails depend on the theme (PDF inversion + page base color).
        if (m_paneBars[i].bound) rebuildThumbnailStrip(static_cast<Pane>(i));
    }
}

void SplitViewManager::setScrollBarsPinned(bool pinned)
{
    if (m_scrollBarsPinned == pinned) {
        return;
    }
    m_scrollBarsPinned = pinned;
    QSettings().setValue(QStringLiteral("scrollbar/pinned"), pinned);

    for (int i = 0; i < 2; ++i) {
        PaneBars& b = m_paneBars[i];
        if (!b.vBar) continue;
        if (pinned) {
            if (b.fadeTimer) b.fadeTimer->stop();
            b.vBar->setVisible(true);
            b.hBar->setVisible(true);
            b.vBar->raise();
            b.hBar->raise();
        } else if (b.fadeTimer) {
            b.fadeTimer->start();  // begin fading the currently-shown bars
        }
    }
}

void SplitViewManager::proximityFloatCheck(QEvent* event)
{
    // Palm rejection: only a real pen (tablet) or a non-finger mouse may arm
    // the float. Touch-synthesized mouse moves are ignored.
    QPointF globalPos;
    if (event->type() == QEvent::TabletMove) {
        globalPos = static_cast<QTabletEvent*>(event)->globalPosition();
    } else {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->pointerType() == QPointingDevice::PointerType::Finger) {
            return;
        }
        globalPos = me->globalPosition();
    }

    checkPaneProximity(Left, globalPos);
    if (isSplit()) {
        checkPaneProximity(Right, globalPos);
    }
}

void SplitViewManager::checkPaneProximity(Pane pane, const QPointF& globalPos)
{
    QStackedWidget* stack = stackForPane(pane);
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!stack || !b.vBar || !stack->isVisible()) return;

    const QPoint local = stack->mapFromGlobal(globalPos.toPoint());
    if (!stack->rect().contains(local)) return;

    // Arm when the pointer is near the docked edges the bars live on
    // (left edge for the vertical bar, top edge for the horizontal bar),
    // including the region the bars themselves occupy.
    const int threshold = 24;
    const bool nearLeft = local.x() <= threshold;
    const bool nearTop = local.y() <= threshold;
    if (nearLeft || nearTop) {
        showScrollBars(pane);
    }
}

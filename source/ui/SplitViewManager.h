#pragma once

// ============================================================================
// SplitViewManager - Manages dual-pane split view with independent tab bars
// ============================================================================
// Coordinates two independent panes (left/right), each with its own TabBar,
// QStackedWidget, and TabManager. Only one pane is "active" at a time; all
// MainWindow UI actions route through the active pane's viewport.
//
// When not split, the left pane occupies the full width and the right pane
// does not exist. Splitting creates the right pane on demand.
// ============================================================================

#include <QWidget>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVector>
#include <QList>
#include <QSet>
#include <QHash>
#include <QSize>
#include <QRect>
#include <QPixmap>
#include <QPointer>

class TabBar;
class TabManager;
class Document;
class DocumentViewport;
class QStackedWidget;
class ViewportScrollBar;
class ThumbnailRenderer;
class QTimer;

class SplitViewManager : public QWidget {
    Q_OBJECT

public:
    enum Pane { Left = 0, Right = 1 };

    explicit SplitViewManager(QWidget* parent = nullptr);
    ~SplitViewManager() override;

    // =========================================================================
    // Active pane
    // =========================================================================

    Pane activePane() const;
    void setActivePane(Pane pane);
    DocumentViewport* activeViewport() const;
    DocumentViewport* inactiveViewport() const;

    // =========================================================================
    // Split control
    // =========================================================================

    bool isSplit() const;

    /**
     * @brief Move a tab from sourcePane to the opposite pane.
     *
     * If not yet split, the right pane is created first.
     * If moving the last tab out of the right pane, the pane is auto-closed.
     */
    void splitTab(int tabIndex, Pane sourcePane);

    /**
     * @brief Move all right-pane tabs to the left pane, then destroy the right pane.
     */
    void mergePanes();

    // =========================================================================
    // Delegated TabManager API (routes to active pane by default)
    // =========================================================================

    int createTab(Document* doc, const QString& title);
    int createTabInPane(Document* doc, const QString& title, Pane pane);

    int totalTabCount() const;
    int activeTabCount() const;

    TabManager* activeTabManager() const;
    TabManager* leftTabManager() const;
    TabManager* rightTabManager() const;

    TabBar* leftTabBar() const;
    TabBar* rightTabBar() const;

    QStackedWidget* leftViewportStack() const;
    QStackedWidget* rightViewportStack() const;

    // =========================================================================
    // Iteration helpers (for session save, close-all, etc.)
    // =========================================================================

    struct TabRef {
        Pane pane;
        int index;
    };
    QVector<TabRef> allTabs() const;

    /**
     * @brief Apply a function to every TabManager (left, and right if split).
     */
    template<typename Func>
    void forEachTabManager(Func fn) {
        if (m_leftTabManager) fn(m_leftTabManager, Left);
        if (m_rightTabManager) fn(m_rightTabManager, Right);
    }

    /**
     * @brief Get the widget row that contains the tab bars (for layout insertion).
     */
    QWidget* tabBarContainer() const;

    /**
     * @brief Get the QSplitter that holds the viewport stacks (for layout insertion).
     */
    QSplitter* viewportSplitter() const;

    /**
     * @brief Apply theme to all tab bars (current and future).
     *
     * Stores the theme settings so that newly created right panes
     * automatically receive the correct theme.
     */
    void updateTheme(bool darkMode, const QColor& accentColor);

    // =========================================================================
    // Enhanced scroll bars (Plan SB1)
    // =========================================================================

    /**
     * @brief Pin the overlay scroll bars visible (disable proximity auto-hide).
     *
     * Persisted to QSettings; SB4's settings UI can flip this live.
     */
    void setScrollBarsPinned(bool pinned);
    bool scrollBarsPinned() const { return m_scrollBarsPinned; }

    /**
     * @brief Recompute the SB2 document map (per-source accents + link markers)
     *        for whichever pane is bound to @p vp and push it to that pane's
     *        vertical bar.
     *
     * Cheap and idempotent; safe to call on any structure/link/theme change.
     * A null @p vp or a vp not currently bound to a pane is a no-op.
     */
    void updateScrollBarDocumentMap(DocumentViewport* vp);

    /**
     * @brief Rebuild the SB3 low-res thumbnail strip for whichever pane is bound
     *        to @p vp (async, memory-bounded, band-sampled).
     *
     * Call on structure/import changes. A null @p vp or a vp not currently bound
     * to a pane is a no-op. Cheap to call; it cancels in-flight work and refeeds.
     */
    void rebuildScrollBarThumbnails(DocumentViewport* vp);

signals:
    void activeViewportChanged(DocumentViewport* viewport);
    void activePaneChanged(Pane pane);
    void tabCloseRequested(int tabId, DocumentViewport* viewport, Pane pane);
    void tabCloseAttempted(int tabId, DocumentViewport* viewport, Pane pane);
    void splitStateChanged(bool isSplit);

    /**
     * @brief Emitted whenever the total number of tabs across both panes changes.
     *
     * Used by MainWindow to auto-hide the tab bar container when only one
     * notebook is open.
     */
    void totalTabCountChanged(int total);

private slots:
    void onLeftViewportChanged(DocumentViewport* vp);
    void onRightViewportChanged(DocumentViewport* vp);
    void onLeftTabCloseAttempted(int index, DocumentViewport* vp);
    void onRightTabCloseAttempted(int index, DocumentViewport* vp);
    void onLeftTabCloseRequested(int index, DocumentViewport* vp);
    void onRightTabCloseRequested(int index, DocumentViewport* vp);

private:
    void createRightPane();
    void destroyRightPane();
    void updateTabBarContainerLayout();
    void updateActivePaneIndicator();
    void recenterAllViewports();
    bool eventFilter(QObject* watched, QEvent* event) override;

    // --- Enhanced scroll bars (SB1) ---
    struct PaneBars {
        ViewportScrollBar* vBar = nullptr;   // page axis (vertical), docked left
        ViewportScrollBar* hBar = nullptr;   // cross axis (horizontal), docked top
        QTimer* fadeTimer = nullptr;
        QPointer<DocumentViewport> bound;
        QMetaObject::Connection cViewToV;
        QMetaObject::Connection cViewToH;
        QMetaObject::Connection cVToView;
        QMetaObject::Connection cHToView;
        QMetaObject::Connection cMarker;   // SB2: vBar markerActivated -> scrollToPage

        // --- SB3: low-res thumbnail strip ---
        // Memory-bounded: exactly ONE composited strip pixmap (sized to the
        // track in device px, not the document); per-page thumbnails are painted
        // into it and discarded, never cached.
        ThumbnailRenderer* thumbRenderer = nullptr;  // per-pane async renderer
        QPixmap strip;                     // single composited strip (bounded by track px)
        QList<int> stripQueue;             // sampled pages awaiting render
        QSet<int> stripInFlight;           // pages requested, awaiting thumbnailReady
        QHash<int, QRect> stripSlots;      // shown page -> its slot rect in strip device px
        QSize stripPxSize;                 // track px size the current strip was built for
        QTimer* stripEditTimer = nullptr;  // debounce visible-page edit -> single slice
        int pendingEditPage = -1;          // page to refresh when stripEditTimer fires
        QMetaObject::Connection cThumbReady;    // thumbRenderer::thumbnailReady
        QMetaObject::Connection cPageModified;  // vp::pageModified -> debounced slice
    };

    QStackedWidget* stackForPane(Pane pane) const;
    DocumentViewport* viewportForPane(Pane pane) const;
    void createScrollBars(Pane pane);
    void destroyScrollBars(Pane pane);
    void repositionScrollBars(Pane pane);
    void bindScrollBars(Pane pane, DocumentViewport* vp);
    void refreshHandleSizes(Pane pane);
    void showScrollBars(Pane pane);
    void hideScrollBars(Pane pane);
    void applyScrollBarDarkMode();

    // --- SB3 helpers ---
    void rebuildThumbnailStrip(Pane pane);
    void refreshThumbnailSlice(Pane pane, int page);
    void feedThumbnailQueue(Pane pane);
    void compositeThumbnailSlice(Pane pane, int page, const QPixmap& pixmap);

    void proximityFloatCheck(QEvent* event);
    void checkPaneProximity(Pane pane, const QPointF& globalPos);
    static bool defaultScrollBarsPinned();

    // Left pane (always exists)
    TabBar* m_leftTabBar = nullptr;
    QStackedWidget* m_leftViewportStack = nullptr;
    TabManager* m_leftTabManager = nullptr;

    // Right pane (created on demand)
    TabBar* m_rightTabBar = nullptr;
    QStackedWidget* m_rightViewportStack = nullptr;
    TabManager* m_rightTabManager = nullptr;

    // Layout
    QWidget* m_tabBarContainer = nullptr;
    QHBoxLayout* m_tabBarLayout = nullptr;
    QSplitter* m_splitter = nullptr;

    Pane m_activePane = Left;

    // Cached theme for applying to newly created panes
    bool m_darkMode = false;
    QColor m_accentColor;

    // Enhanced scroll bars (SB1), indexed by Pane (Left=0, Right=1)
    PaneBars m_paneBars[2];
    bool m_scrollBarsPinned = true;

    // SB3: coalesces resize ticks into a single strip rebuild per pane whose
    // track size actually changed (avoids thrashing cancelAll() during drag).
    QTimer* m_stripResizeTimer = nullptr;
};

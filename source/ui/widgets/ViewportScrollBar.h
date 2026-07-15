// ============================================================================
// ViewportScrollBar - custom overlay scroll bar for the paged viewport
// ============================================================================
// Replaces the pre-remake overlay QScrollBar pair (panXSlider/panYSlider).
// Keeps the exact fraction-based value contract (0.0-1.0) so the viewport
// coupling is unchanged, but is a fully custom-painted QWidget so later plans
// can paint per-source accents (SB2), link/search markers (SB2), and a
// low-res thumbnail strip (SB3) inside it. Orientation and docked edge are
// parameterized (never hard-coded) so SB4 can reposition/reorient it.
//
// SB1 scope: value handling, drag/track-page interaction, proximity-float
// visibility (driven externally), and theming. No rich content yet, but the
// paint path already reserves lanes (accent stripe / marker / handle) so the
// later plans slot in without a redesign.
// ============================================================================

#ifndef VIEWPORTSCROLLBAR_H
#define VIEWPORTSCROLLBAR_H

#include <QWidget>
#include <QColor>
#include <QString>
#include <QVector>
#include <QPixmap>
#include <QSize>

class ViewportScrollBar : public QWidget {
    Q_OBJECT

public:
    // Which edge of the host the bar is docked against. Stored for SB4
    // (placement/orientation settings); SB1 paints symmetrically.
    enum class DockEdge { Left, Right, Top, Bottom };

    // SB2 document map -------------------------------------------------------
    // A marker's kind decides its lane/tooltip semantics. Only Link is produced
    // by SB2; SearchHit is reserved for SB-search (channel is ready).
    enum class MarkerKind { Link, SearchHit };

    // A continuous per-source color band along the track, in track fractions
    // [0..1] (top-of-first-page .. bottom-of-last-page of the run).
    struct AccentRegion {
        qreal start = 0.0;
        qreal end = 0.0;
        QColor color;
    };

    // A single tick at a track fraction. pageIndex is the jump target.
    struct BarMarker {
        qreal pos = 0.0;
        QColor color;
        int pageIndex = -1;
        MarkerKind kind = MarkerKind::Link;
        QString tooltip;
    };

    explicit ViewportScrollBar(Qt::Orientation orientation,
                               DockEdge edge,
                               QWidget* parent = nullptr);

    Qt::Orientation orientation() const { return m_orientation; }
    DockEdge dockEdge() const { return m_edge; }
    void setDockEdge(DockEdge edge);

    // Current scroll position, 0.0 (top/left) .. 1.0 (bottom/right).
    qreal fraction() const { return m_fraction; }
    // Handle length as a fraction of the track (visible/content ratio).
    qreal handleFraction() const { return m_handleFraction; }

    bool isDragging() const { return m_dragging; }

    // The fixed thickness of the bar along its minor axis. The page-axis
    // (vertical) bar is wider so its low-res thumbnail filmstrip is legible;
    // the cross-axis (horizontal) bar keeps the slim default.
    static int barThickness(Qt::Orientation o) { return o == Qt::Vertical ? 32 : 16; }

    // SB2: document-map content. Both are no-ops on horizontal bars (the map
    // is a page-axis concept). Stored and repainted; positions are track
    // fractions already mapped by the controller.
    void setAccentRegions(const QVector<AccentRegion>& regions);
    void setMarkers(const QVector<BarMarker>& markers);

    // SB3: low-res thumbnail strip painted as the track background (behind the
    // accent stripe and marker ticks). No-op on horizontal bars (page-axis
    // concept). The controller composites and sizes the strip; the bar just
    // paints it clipped to the rounded track.
    void setThumbnailStrip(const QPixmap& strip);

    // SB3: device-pixel size of the track's drawable rect, so the controller can
    // allocate a strip pixmap that maps 1:1 onto the painted track. Returns an
    // empty size when there is no usable track.
    QSize trackContentPixelSize() const;

public slots:
    // Programmatic position update (from the viewport). Does NOT emit
    // fractionChanged, so it cannot feed back into the viewport.
    void setFraction(qreal fraction);
    // Handle length as fraction of the track (clamped to a touch-friendly min).
    void setHandleFraction(qreal frac);
    void setDarkMode(bool dark);

signals:
    // Emitted only on user interaction (drag / track paging).
    void fractionChanged(qreal fraction);
    // Emitted when the user clicks a marker tick (SB2). pageIndex is the
    // marker's jump target.
    void markerActivated(int pageIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool isVertical() const { return m_orientation == Qt::Vertical; }
    qreal trackMargin() const { return 2.0; }
    qreal trackLength() const;              // usable length along the major axis
    qreal handleLengthPx() const;           // handle length in pixels
    qreal handleStartPx() const;            // handle start (top/left) in pixels
    qreal posAlongAxis(const QPointF& p) const;
    void setFractionFromUser(qreal fraction);

    // SB2 helpers.
    qreal fractionToPx(qreal frac) const;   // track-fraction -> pixel along axis
    int markerAtPos(qreal pos) const;       // index into m_markers within hit band, or -1
    QColor legibleMarkerColor(const QColor& raw) const;

    QColor handleColor() const;

    Qt::Orientation m_orientation;
    DockEdge m_edge;

    qreal m_fraction = 0.0;        // 0..1 scroll position
    qreal m_handleFraction = 0.25; // 0..1 visible/content ratio
    bool m_darkMode = false;

    bool m_dragging = false;
    bool m_handleHovered = false;
    qreal m_dragGrabOffset = 0.0;  // px between grab point and handle start
    int m_hoveredMarker = -1;      // SB2: marker index under cursor (tooltip debounce)

    QVector<AccentRegion> m_accents;  // SB2: per-source color bands
    QVector<BarMarker> m_markers;     // SB2: link/search ticks
    QPixmap m_thumbStrip;             // SB3: composited low-res page strip

    static constexpr int kMinHandlePx = 40;
    static constexpr qreal kMarkerHitBandPx = 6.0;  // half-width of tick hit zone
};

#endif // VIEWPORTSCROLLBAR_H

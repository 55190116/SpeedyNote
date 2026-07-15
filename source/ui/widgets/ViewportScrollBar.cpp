// ============================================================================
// ViewportScrollBar Implementation
// ============================================================================

#include "ViewportScrollBar.h"

#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QtGlobal>
#include <algorithm>

ViewportScrollBar::ViewportScrollBar(Qt::Orientation orientation,
                                     DockEdge edge,
                                     QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_edge(edge)
{
    // Overlay: transparent background, floats above the viewport content.
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    if (isVertical()) {
        setFixedWidth(barThickness());
    } else {
        setFixedHeight(barThickness());
    }
}

void ViewportScrollBar::setDockEdge(DockEdge edge)
{
    if (m_edge == edge) return;
    m_edge = edge;
    update();
}

void ViewportScrollBar::setDarkMode(bool dark)
{
    if (m_darkMode == dark) return;
    m_darkMode = dark;
    update();
}

void ViewportScrollBar::setFraction(qreal fraction)
{
    // Programmatic update from the viewport. Ignore while the user is
    // dragging so the viewport's echo cannot fight the drag.
    if (m_dragging) return;
    fraction = qBound(0.0, fraction, 1.0);
    if (qFuzzyCompare(fraction + 1.0, m_fraction + 1.0)) return;
    m_fraction = fraction;
    update();
}

void ViewportScrollBar::setHandleFraction(qreal frac)
{
    frac = qBound(0.0, frac, 1.0);
    if (qFuzzyCompare(frac + 1.0, m_handleFraction + 1.0)) return;
    m_handleFraction = frac;
    update();
}

void ViewportScrollBar::setAccentRegions(const QVector<AccentRegion>& regions)
{
    // Document-map is a page-axis concept; ignore on horizontal bars.
    if (!isVertical()) {
        if (!m_accents.isEmpty()) { m_accents.clear(); update(); }
        return;
    }
    m_accents = regions;
    update();
}

void ViewportScrollBar::setMarkers(const QVector<BarMarker>& markers)
{
    if (!isVertical()) {
        if (!m_markers.isEmpty()) { m_markers.clear(); update(); }
        return;
    }
    m_markers = markers;
    update();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

qreal ViewportScrollBar::trackLength() const
{
    const qreal len = isVertical() ? height() : width();
    return qMax(0.0, len - 2.0 * trackMargin());
}

qreal ViewportScrollBar::handleLengthPx() const
{
    const qreal track = trackLength();
    if (track <= 0.0) return 0.0;
    qreal len = m_handleFraction * track;
    len = qBound(static_cast<qreal>(kMinHandlePx), len, track);
    return len;
}

qreal ViewportScrollBar::handleStartPx() const
{
    const qreal track = trackLength();
    const qreal handle = handleLengthPx();
    const qreal travel = qMax(0.0, track - handle);
    return trackMargin() + m_fraction * travel;
}

qreal ViewportScrollBar::posAlongAxis(const QPointF& p) const
{
    return isVertical() ? p.y() : p.x();
}

void ViewportScrollBar::setFractionFromUser(qreal fraction)
{
    fraction = qBound(0.0, fraction, 1.0);
    if (qFuzzyCompare(fraction + 1.0, m_fraction + 1.0)) return;
    m_fraction = fraction;
    update();
    emit fractionChanged(m_fraction);
}

qreal ViewportScrollBar::fractionToPx(qreal frac) const
{
    // Same trackLength() basis as the handle top, so a page marker lands where
    // the handle sits when that page reaches the top (see SB2 derivation).
    return trackMargin() + qBound(0.0, frac, 1.0) * trackLength();
}

int ViewportScrollBar::markerAtPos(qreal pos) const
{
    int best = -1;
    qreal bestDist = kMarkerHitBandPx;
    for (int i = 0; i < m_markers.size(); ++i) {
        const qreal mp = fractionToPx(m_markers[i].pos);
        const qreal d = qAbs(pos - mp);
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

QColor ViewportScrollBar::legibleMarkerColor(const QColor& raw) const
{
    if (!raw.isValid()) {
        return m_darkMode ? QColor(210, 210, 210) : QColor(90, 90, 90);
    }
    // LinkObject's default icon color is a translucent mid-gray (100,100,100).
    // On the mid-gray track it disappears, so substitute a theme-legible gray.
    const bool nearDefaultGray =
        qAbs(raw.red()   - 100) <= 12 &&
        qAbs(raw.green() - 100) <= 12 &&
        qAbs(raw.blue()  - 100) <= 12;
    if (nearDefaultGray) {
        return m_darkMode ? QColor(210, 210, 210) : QColor(70, 70, 70);
    }
    QColor c = raw;
    c.setAlpha(255);  // ticks are opaque regardless of the source alpha
    return c;
}

// ---------------------------------------------------------------------------
// Theming
// ---------------------------------------------------------------------------

QColor ViewportScrollBar::trackColor() const
{
    return m_darkMode ? QColor(255, 255, 255, 24)
                      : QColor(0, 0, 0, 26);
}

QColor ViewportScrollBar::handleColor() const
{
    const bool active = m_dragging || m_handleHovered;
    if (m_darkMode) {
        return active ? QColor(235, 235, 235, 220) : QColor(200, 200, 200, 150);
    }
    return active ? QColor(70, 70, 70, 220) : QColor(100, 100, 100, 160);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void ViewportScrollBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    // Lane layout (reserved for later plans):
    //   - background/track (drawn here in SB1)
    //   - source-accent stripe + thumbnail strip (SB2/SB3, behind the handle)
    //   - marker ticks (SB2, above the strip)
    //   - the drag handle (drawn here, floats above all lanes)

    // Track background spanning the full minor axis (same rect for either
    // orientation; only the rounding axis differs).
    const QRectF track(trackMargin(), trackMargin(),
                       width() - 2.0 * trackMargin(),
                       height() - 2.0 * trackMargin());
    const qreal trackRadius = (isVertical() ? track.width() : track.height()) / 2.0;
    p.setBrush(trackColor());
    p.drawRoundedRect(track, trackRadius, trackRadius);

    // SB2: per-source accent stripes — a thin band on the docked (far) edge,
    // behind the handle. Vertical/page-axis only.
    if (isVertical() && !m_accents.isEmpty()) {
        const qreal stripeW = 3.0;
        const bool rightEdge = (m_edge == DockEdge::Right);
        const qreal stripeX = rightEdge ? (width() - trackMargin() - stripeW)
                                         : trackMargin();
        for (const AccentRegion& r : m_accents) {
            if (!r.color.isValid()) continue;
            const qreal y0 = fractionToPx(r.start);
            const qreal y1 = fractionToPx(r.end);
            if (y1 <= y0) continue;
            QColor c = r.color;
            c.setAlpha(m_darkMode ? 190 : 170);
            p.setBrush(c);
            p.drawRoundedRect(QRectF(stripeX, y0, stripeW, y1 - y0), 1.0, 1.0);
        }
    }

    // Handle.
    const qreal start = handleStartPx();
    const qreal len = handleLengthPx();

    if (len > 0.0) {
        const qreal inset = 2.0;
        QRectF handle;
        if (isVertical()) {
            handle = QRectF(inset, start, qMax(0.0, width() - 2.0 * inset), len);
        } else {
            handle = QRectF(start, inset, len, qMax(0.0, height() - 2.0 * inset));
        }
        const qreal handleRadius = (isVertical() ? handle.width() : handle.height()) / 2.0;
        p.setBrush(handleColor());
        p.drawRoundedRect(handle, handleRadius, handleRadius);
    }

    // SB2: marker ticks on top of the handle so they stay visible. Thin,
    // near-full-width opaque ticks colored per link. Vertical only.
    if (isVertical() && !m_markers.isEmpty()) {
        const qreal tickInset = 1.5;
        const qreal tickX = tickInset;
        const qreal tickW = qMax(0.0, width() - 2.0 * tickInset);
        const qreal tickH = 3.0;
        for (const BarMarker& m : m_markers) {
            const qreal y = fractionToPx(m.pos) - tickH / 2.0;
            p.setBrush(legibleMarkerColor(m.color));
            p.drawRoundedRect(QRectF(tickX, y, tickW, tickH), 1.0, 1.0);
        }
    }
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void ViewportScrollBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const qreal pos = posAlongAxis(event->position());
    const qreal start = handleStartPx();
    const qreal len = handleLengthPx();

    if (pos >= start && pos <= start + len) {
        // Handle drag wins over marker hits when the press is on the handle.
        m_dragging = true;
        m_dragGrabOffset = pos - start;
        update();
    } else if (const int mi = markerAtPos(pos); mi >= 0) {
        // SB2: click a marker tick to jump to its page.
        const int page = m_markers[mi].pageIndex;
        if (page >= 0) emit markerActivated(page);
    } else {
        // Track click: page toward the click (QScrollBar-like feel).
        const qreal page = qMax(m_handleFraction, 0.1);
        const qreal delta = (pos < start) ? -page : page;
        setFractionFromUser(m_fraction + delta);
    }
    event->accept();
}

void ViewportScrollBar::mouseMoveEvent(QMouseEvent* event)
{
    const qreal pos = posAlongAxis(event->position());

    if (m_dragging) {
        const qreal track = trackLength();
        const qreal handle = handleLengthPx();
        const qreal travel = qMax(0.0, track - handle);
        if (travel > 0.0) {
            const qreal newStart = pos - m_dragGrabOffset;
            setFractionFromUser((newStart - trackMargin()) / travel);
        }
        event->accept();
        return;
    }

    // Hover feedback on the handle.
    const qreal start = handleStartPx();
    const qreal len = handleLengthPx();
    const bool hovered = (pos >= start && pos <= start + len);
    if (hovered != m_handleHovered) {
        m_handleHovered = hovered;
        update();
    }

    // SB2: marker tooltip (only when not over the handle). Debounced on the
    // hovered-marker index so we don't re-fire QToolTip on every move.
    const int mi = hovered ? -1 : markerAtPos(pos);
    if (mi != m_hoveredMarker) {
        m_hoveredMarker = mi;
        if (mi >= 0 && !m_markers[mi].tooltip.isEmpty()) {
            QToolTip::showText(event->globalPosition().toPoint(),
                               m_markers[mi].tooltip, this);
        } else {
            QToolTip::hideText();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ViewportScrollBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewportScrollBar::leaveEvent(QEvent* event)
{
    if (m_handleHovered) {
        m_handleHovered = false;
        update();
    }
    m_hoveredMarker = -1;
    QWidget::leaveEvent(event);
}

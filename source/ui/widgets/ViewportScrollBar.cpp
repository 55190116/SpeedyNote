// ============================================================================
// ViewportScrollBar Implementation
// ============================================================================

#include "ViewportScrollBar.h"

#include <QPainter>
#include <QMouseEvent>
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

    // Track background spanning the full minor axis.
    QRectF track;
    if (isVertical()) {
        track = QRectF(trackMargin(), trackMargin(),
                       width() - 2.0 * trackMargin(),
                       height() - 2.0 * trackMargin());
    } else {
        track = QRectF(trackMargin(), trackMargin(),
                       width() - 2.0 * trackMargin(),
                       height() - 2.0 * trackMargin());
    }
    const qreal trackRadius = (isVertical() ? track.width() : track.height()) / 2.0;
    p.setBrush(trackColor());
    p.drawRoundedRect(track, trackRadius, trackRadius);

    // Handle.
    const qreal start = handleStartPx();
    const qreal len = handleLengthPx();
    if (len <= 0.0) return;

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
        // Grab the handle for dragging.
        m_dragging = true;
        m_dragGrabOffset = pos - start;
        update();
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
    QWidget::leaveEvent(event);
}

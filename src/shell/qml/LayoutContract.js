.pragma library

/**
 * Geometry for the edge-docked fan.
 *
 * Kept out of Main.qml so the placement rules can be reasoned about — and exercised —
 * without a compositor. Every function here is pure: same arguments, same result, no
 * reads of live scene state.
 *
 * The deck docks against the right screen edge. The `edge` parameter is carried through
 * the signatures so a future left/top/bottom placement has somewhere to live, but only
 * "right" is implemented today and anything else is treated as "right".
 */

/** Outward lift applied to a stick, in scene pixels.
 *
 *  A hovered stick lifts furthest, the current note lifts a little so it reads as
 *  selected at rest, and everything else sits flush. Lift moves the painted face toward
 *  the desktop, which on a right-docked deck is negative x; y never changes, because the
 *  fan's vertical order is the user's own arrangement.
 */
function stickLift(edge, isCurrent, isHovered) {
    var distance = isHovered ? 8 : (isCurrent ? 4 : 0)
    return { x: -distance, y: 0 }
}

/** Stacking order for a stick.
 *
 *  Hover wins outright, so the stick under the pointer is never occluded by its
 *  neighbours. The current note sits above the resting deck. Resting sticks descend by
 *  index, which is what produces the shingled overlap: lower indices paint on top.
 */
function stickLayer(index, isCurrent, isHovered, count) {
    if (isHovered) {
        return 3 * count + 300
    }
    if (isCurrent) {
        return 2 * count + 200
    }
    return count - index + 100
}

/** Height of a stick's clickable strip.
 *
 *  The front stick is fully exposed and takes its whole length. Every stick behind it is
 *  overlapped by its neighbour, so only one pitch of it is reachable — claiming more
 *  would put its hit area underneath the stick painted on top, and clicks would land on
 *  the wrong note.
 */
function stickHitLength(tabLength, pitch, index, count) {
    if (index <= 0) {
        return tabLength
    }
    return Math.min(tabLength, pitch)
}

/** Scroll range for a deck whose natural extent may exceed its fixed viewport. */
function fanScrollMaximum(count, pitch, tabLength, viewportHeight) {
    if (count <= 0) {
        return 0
    }
    return Math.max(0, (count - 1) * pitch + tabLength - viewportHeight)
}

/** A downward wheel moves the upward-growing deck down to reveal farther sticks.
 * Qt reports both angleDelta.y and pixelDelta.y negative for downward motion. */
function fanWheelDelta(angleY, pixelY, pitch) {
    return -(pixelY !== 0 ? pixelY : angleY / 120 * Math.max(36, pitch))
}

/** Smallest offset change that puts a requested stick wholly inside the viewport. */
function fanOffsetForIndex(index, currentOffset, maximumOffset, baseY, pitch, tabLength,
                           viewportTop, viewportHeight) {
    if (index < 0 || viewportHeight <= 0) {
        return Math.max(0, Math.min(maximumOffset, currentOffset))
    }
    var offset = currentOffset
    var top = baseY - index * pitch + offset - viewportTop
    if (top < 0) {
        offset -= top
    } else if (top + tabLength > viewportHeight) {
        offset -= top + tabLength - viewportHeight
    }
    return Math.max(0, Math.min(maximumOffset, offset))
}

/** Window position along the docking axis.
 *
 *  Measured against the work area rather than the raw screen, so a panel on the docking
 *  edge pushes the deck inward instead of hiding underneath it.
 */
function dialogEdgeAxisPosition(availablePosition, availableLength, windowLength, edge) {
    if (edge === "left") {
        return availablePosition
    }
    return availablePosition + availableLength - windowLength
}

/** How much of a bottom panel strip the dock must still keep clear of itself.
 *
 *  `reserve` is measured from the SCREEN's bottom edge. On Wayland a client reads back the
 *  whole screen as its work area, so the task manager is invisible to it and the reserve
 *  must clear it unaided; where the work area does exclude a panel, only the part of the
 *  reserve it has not already excluded is added, so the panel is never counted twice.
 */
function panelClearance(screenY, screenHeight, availableY, availableHeight, reserve) {
    var excluded = (screenY + screenHeight) - (availableY + availableHeight)
    return Math.max(0, reserve - Math.max(0, excluded))
}

/** The lane's inset from the dock window's bottom edge.
 *
 *  `screenGap` is the lane's clearance from the bottom of the work area as reported. The
 *  window already stops `clearance` above that, so the inset shrinks by the same amount and
 *  the deck stays where it was on screen.
 */
function fanBottomInset(screenGap, clearance) {
    return Math.max(0, screenGap - clearance)
}

/** Dock window y: bottom-anchored when collapsed, centred otherwise, inside the working
 *  height (the work area less the panel clearance).
 */
function dockWindowY(availableY, workHeight, windowHeight, centred) {
    return centred
        ? availableY + Math.max(0, Math.round((workHeight - windowHeight)/2))
        : availableY + Math.max(0, workHeight - windowHeight)
}

/** Height available between a surface-local panel and a footer inside the paper.
 *
 *  The footer's y is local to the paper while the panel's y is local to their shared
 *  surface. Translate the footer before comparing them; otherwise a vertically offset
 *  card can make a real cap look invalid and leave the panel too tall to scroll.
 */
function libraryPanelCap(paperY, footerY, panelY, minimumHeight, gap) {
    var footerSurfaceY = paperY + footerY
    return footerSurfaceY > panelY + minimumHeight ? footerSurfaceY - panelY - gap : 100000
}

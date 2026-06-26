#include "app.h"

/*
    The scrollbar's value is never tracked as an independent counter --
    it's always derived fresh from TextEdit's own destRect vs. viewRect,
    so it can't drift out of sync with where the text actually is. Every
    scroll operation below scrolls by (current real offset - desired
    offset) and then re-reads the real offset afterward, rather than
    trusting an incrementally-adjusted running total.
*/
static short CurrentScrollOffset(TEHandle te)
{
    return (**te).viewRect.top - (**te).destRect.top;
}

static void SyncScrollbarToOffset(void)
{
    short newValue = CurrentScrollOffset(gActiveTE);

    /* SetControlValue always redraws the control, even when the value is
       unchanged -- called every tick, an unguarded call here would redraw
       the scrollbar (and the flicker that comes with it) on every single
       keystroke for no reason. */
    if (newValue != GetControlValue(gScrollBar))
        SetControlValue(gScrollBar, newValue);
}

/*
    Updates the scrollbar's range/visibility only -- no clamping of the
    current position. Used on the typing path, where ScrollCaretIntoView
    already owns getting the position right; re-deriving maxVal from
    TEGetHeight for a line that's actively growing as you type is exactly
    the kind of thing that could disagree with ScrollCaretIntoView's own
    (separately computed) target by a pixel or two, and clamping on that
    discrepancy every keystroke is what was causing a brief upward jump.
*/
void UpdateScrollbarRange(void)
{
    long textHeight;
    short viewHeight;
    short maxVal;
    Boolean shouldShow;

    textHeight = TEGetHeight((**gActiveTE).nLines, 0, gActiveTE);
    viewHeight = (**gActiveTE).viewRect.bottom - (**gActiveTE).viewRect.top;

    maxVal = (textHeight > viewHeight) ? (short) (textHeight - viewHeight) : 0;

    if (maxVal != GetControlMaximum(gScrollBar))
        SetControlMaximum(gScrollBar, maxVal);

    shouldShow = (maxVal > 0);
    if (shouldShow != gScrollBarVisible) {
        if (shouldShow)
            ShowControl(gScrollBar);
        else
            HideControl(gScrollBar);
        gScrollBarVisible = shouldShow;
    }
}

/*
    Full version: also clamps the current scroll position if it now
    exceeds the (possibly shrunk) range. Needed after anything that can
    reduce content height -- Style commands, zoom, load/new, mode switch
    -- but not after plain typing, which only ever grows it.
*/
void AdjustScrollbar(void)
{
    short maxVal;
    short curOffset;

    UpdateScrollbarRange();

    maxVal = GetControlMaximum(gScrollBar);
    curOffset = CurrentScrollOffset(gActiveTE);
    if (curOffset > maxVal)
        TEScroll(0, curOffset - maxVal, gActiveTE);
    else if (curOffset < 0)
        TEScroll(0, curOffset, gActiveTE);

    SyncScrollbarToOffset();
}

static short LineContaining(TEHandle te, short pos)
{
    short line = 0;

    while (line < (**te).nLines - 1 && (**te).lineStarts[line + 1] <= pos)
        line++;
    return line;
}

void ScrollCaretIntoView(void)
{
    short caretLine;
    short lineTop, lineBottom;
    short viewTop, viewBottom;

    caretLine = LineContaining(gActiveTE, (**gActiveTE).selEnd);

    /* Querying a single line's height in isolation (e.g. TEGetHeight
       for just [caretLine, caretLine+1)) comes back unreliable right
       after Enter creates a new, still-empty line -- it hasn't
       "settled" with any content yet. (**te).lineHeight turned out
       to have the same problem, returning a stale/wrong value rather
       than tracking the actual current font size. Avoid isolated
       single-line queries entirely: always sum cumulatively from the
       very start of the document, the same pattern already proven
       reliable in UpdateScrollbarRange's TEGetHeight(nLines, 0, ...). */
    lineTop = (**gActiveTE).destRect.top + TEGetHeight(caretLine, 0, gActiveTE);
    lineBottom = (**gActiveTE).destRect.top + TEGetHeight(caretLine + 1, 0, gActiveTE);

    viewTop = (**gActiveTE).viewRect.top;
    viewBottom = (**gActiveTE).viewRect.bottom;

    if (lineBottom > viewBottom)
        TEScroll(0, viewBottom - lineBottom, gActiveTE);
    else if (lineTop < viewTop)
        TEScroll(0, viewTop - lineTop, gActiveTE);

    SyncScrollbarToOffset();
}

static pascal void ScrollAction(ControlHandle control, short part)
{
    short max, delta, desired;
    short pageSize;

    if (part == 0)
        return;

    max = GetControlMaximum(control);
    pageSize = (**gActiveTE).viewRect.bottom - (**gActiveTE).viewRect.top;

    switch (part) {
        case inUpButton:   delta = -16; break;
        case inDownButton: delta = 16; break;
        case inPageUp:     delta = -pageSize; break;
        case inPageDown:   delta = pageSize; break;
        default:           delta = 0; break;
    }

    desired = CurrentScrollOffset(gActiveTE) + delta;
    if (desired < 0) desired = 0;
    if (desired > max) desired = max;

    TEScroll(0, CurrentScrollOffset(gActiveTE) - desired, gActiveTE);
    SetControlValue(control, CurrentScrollOffset(gActiveTE));
}

void DoScrollClick(Point pt)
{
    ControlHandle control;
    short part;
    short desired;

    part = FindControl(pt, gWindow, &control);
    if (part == 0 || control != gScrollBar)
        return;

    if (part == inThumb) {
        TrackControl(gScrollBar, pt, NULL);
        desired = GetControlValue(gScrollBar);
        TEScroll(0, CurrentScrollOffset(gActiveTE) - desired, gActiveTE);
        SyncScrollbarToOffset();
    } else {
        TrackControl(gScrollBar, pt, NewControlActionUPP(ScrollAction));
    }
}

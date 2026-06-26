#include "app.h"

/* Zoom levels (point deltas from FONT_SIZE). 12/14/18/24pt have a real
   Times bitmap -- confirmed by reading the FOND resource directly rather
   than assuming. The 30pt level has no native bitmap (24pt is the
   largest this font has) and renders as a scaled enlargement of the
   24pt bitmap instead -- a known, accepted tradeoff for going bigger. */
static short kZoomLevels[] = { -6, -4, 0, 6, 12 };

short CurrentFontSize(void)
{
    return FONT_SIZE + kZoomLevels[gZoomIndex];
}

void LoadZoomPref(void)
{
    Handle prefH = GetResource(kZoomPrefType, kZoomPrefID);

    if (prefH != NULL) {
        HLock(prefH);
        gZoomIndex = *(short *) *prefH;
        HUnlock(prefH);
        ReleaseResource(prefH);
        if (gZoomIndex < 0 || gZoomIndex >= kNumZoomLevels)
            gZoomIndex = kZoomBaselineIndex;
    }
}

static void SaveZoomPref(void)
{
    Handle prefH = GetResource(kZoomPrefType, kZoomPrefID);

    if (prefH != NULL) {
        HLock(prefH);
        *(short *) *prefH = gZoomIndex;
        HUnlock(prefH);
        ChangedResource(prefH);
        WriteResource(prefH);
        ReleaseResource(prefH);
    }
}

/*
    Remaps any run whose size matches one of the OLD base/heading sizes
    to the corresponding NEW size, in place -- used for zoom, so it
    never re-parses markdown and can't clobber unsynced edits in
    whichever buffer isn't currently canonical.
*/
static void RescaleStyles(TEHandle te, short oldBase, short newBase)
{
    long len = (**te).teLength;
    long i = 0;
    short savedStart = (**te).selStart;
    short savedEnd = (**te).selEnd;

    while (i < len) {
        TextStyle st;
        short lh, fa;
        long runStart = i;
        short oldSize;
        short newSize;

        TEGetStyle((short) i, &st, &lh, &fa, te);
        oldSize = st.tsSize;

        while (i < len) {
            TextStyle st2;

            TEGetStyle((short) i, &st2, &lh, &fa, te);
            if (st2.tsSize != oldSize)
                break;
            i++;
        }

        if (oldSize == oldBase) newSize = newBase;
        else if (oldSize == oldBase + 12) newSize = newBase + 12;
        else if (oldSize == oldBase + 8) newSize = newBase + 8;
        else if (oldSize == oldBase + 4) newSize = newBase + 4;
        else newSize = oldSize + (newBase - oldBase);

        if (newSize != oldSize) {
            TextStyle ts;

            ts.tsSize = newSize;
            TESetSelect((short) runStart, (short) i, te);
            TESetStyle(doSize, &ts, true, te);
        }
    }

    TESetSelect(savedStart, savedEnd, te);
}

static void ApplyZoomIndex(short newIndex)
{
    short oldBase;
    short newBase;

    if (newIndex < 0 || newIndex >= kNumZoomLevels || newIndex == gZoomIndex)
        return;

    oldBase = CurrentFontSize();
    gZoomIndex = newIndex;
    newBase = CurrentFontSize();

    ClearStyles();
    RescaleStyles(gHiddenTE, oldBase, newBase);
    SaveZoomPref();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

void DoZoom(short direction)
{
    ApplyZoomIndex(gZoomIndex + direction);
}

void DoZoomReset(void)
{
    ApplyZoomIndex(kZoomBaselineIndex);
}

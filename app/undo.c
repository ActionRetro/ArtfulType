#include "app.h"

static void FreeSnapshot(UndoSnapshot *snap)
{
    if (snap->textH != NULL)
        DisposeHandle(snap->textH);
    snap->textH = NULL;
}

void ClearUndoRedoStacks(void)
{
    short i;

    for (i = 0; i < gUndoCount; i++)
        FreeSnapshot(&gUndoStack[i]);
    gUndoCount = 0;
    for (i = 0; i < gRedoCount; i++)
        FreeSnapshot(&gRedoStack[i]);
    gRedoCount = 0;
    gTypingRunActive = false;
}

void UpdateEditMenuState(void)
{
    EnableItem(gEditMenu, iUndo);
    EnableItem(gEditMenu, iRedo);
    if (gUndoCount == 0)
        DisableItem(gEditMenu, iUndo);
    if (gRedoCount == 0)
        DisableItem(gEditMenu, iRedo);
}

/*
    Captures the current document (always as canonical markdown text,
    syncing first if Writer mode is active) onto the undo stack, and
    clears the redo stack -- any new edit invalidates whatever could
    have been redone. Bounded: pushing past MAX_UNDO_LEVELS evicts the
    oldest entry rather than growing unboundedly.
*/
void PushUndoSnapshot(void)
{
    UndoSnapshot *slot;
    Handle textH;
    long len;
    short i;

    if (gHideMarkdown)
        SyncHiddenToCanonical();

    len = (**gTE).teLength;
    textH = NewHandle(len);
    HLock(textH);
    HLock((**gTE).hText);
    BlockMove(*(**gTE).hText, *textH, len);
    HUnlock((**gTE).hText);
    HUnlock(textH);

    if (gUndoCount == MAX_UNDO_LEVELS) {
        FreeSnapshot(&gUndoStack[0]);
        for (i = 0; i < MAX_UNDO_LEVELS - 1; i++)
            gUndoStack[i] = gUndoStack[i + 1];
        gUndoCount--;
    }

    slot = &gUndoStack[gUndoCount++];
    slot->textH = textH;
    slot->length = len;
    slot->selStart = (**gActiveTE).selStart;
    slot->selEnd = (**gActiveTE).selEnd;

    for (i = 0; i < gRedoCount; i++)
        FreeSnapshot(&gRedoStack[i]);
    gRedoCount = 0;

    UpdateEditMenuState();
}

/* Same idea as PushUndoSnapshot, but onto the redo stack -- called
   right before undoing, so redoing can bring the undone state back. */
static void PushRedoSnapshot(void)
{
    UndoSnapshot *slot;
    Handle textH;
    long len;
    short i;

    if (gHideMarkdown)
        SyncHiddenToCanonical();

    len = (**gTE).teLength;
    textH = NewHandle(len);
    HLock(textH);
    HLock((**gTE).hText);
    BlockMove(*(**gTE).hText, *textH, len);
    HUnlock((**gTE).hText);
    HUnlock(textH);

    if (gRedoCount == MAX_UNDO_LEVELS) {
        FreeSnapshot(&gRedoStack[0]);
        for (i = 0; i < MAX_UNDO_LEVELS - 1; i++)
            gRedoStack[i] = gRedoStack[i + 1];
        gRedoCount--;
    }

    slot = &gRedoStack[gRedoCount++];
    slot->textH = textH;
    slot->length = len;
    slot->selStart = (**gActiveTE).selStart;
    slot->selEnd = (**gActiveTE).selEnd;
}

/* Replaces gTE's text with a snapshot and, if Writer mode is active,
   rebuilds gHiddenTE from it so styling comes back correctly. Doesn't
   free the snapshot -- the caller (DoUndo/DoRedo) owns that. */
static void RestoreSnapshot(UndoSnapshot *snap)
{
    Rect savedViewRect;

    SuppressDrawing(gTE, &savedViewRect);
    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    HLock(snap->textH);
    TEInsert(*snap->textH, snap->length, gTE);
    HUnlock(snap->textH);
    RestoreDrawing(gTE, &savedViewRect);

    if (gHideMarkdown) {
        BuildHiddenView();
        TESetSelect(snap->selStart, snap->selEnd, gHiddenTE);
    } else {
        ClearStyles();
        TESetSelect(snap->selStart, snap->selEnd, gTE);
    }

    gDirty = true;
    gTypingRunActive = false;
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

void DoUndo(void)
{
    UndoSnapshot snap;

    if (gUndoCount == 0)
        return;

    PushRedoSnapshot();

    gUndoCount--;
    snap = gUndoStack[gUndoCount];
    RestoreSnapshot(&snap);
    FreeSnapshot(&snap);

    UpdateEditMenuState();
}

void DoRedo(void)
{
    UndoSnapshot snap;

    if (gRedoCount == 0)
        return;

    /* Take the redo entry before pushing onto undo -- PushUndoSnapshot
       unconditionally clears the redo stack (correct for a genuine new
       edit, but redoing isn't one; grab what's needed first). */
    gRedoCount--;
    snap = gRedoStack[gRedoCount];

    PushUndoSnapshot();

    RestoreSnapshot(&snap);
    FreeSnapshot(&snap);

    UpdateEditMenuState();
}

void DoCut(void)
{
    short selStart, selEnd;
    long selLen;
    Handle scrapText;

    selStart = (**gActiveTE).selStart;
    selEnd = (**gActiveTE).selEnd;
    if (selStart == selEnd)
        return;

    if (gHideMarkdown) {
        scrapText = EncodeSelectionAsMarkdown(selStart, selEnd, gActiveTE);
    } else {
        Handle textH = (**gActiveTE).hText;

        selLen = selEnd - selStart;
        scrapText = NewHandle(selLen);
        HLock(textH);
        HLock(scrapText);
        BlockMove(*textH + selStart, *scrapText, selLen);
        HUnlock(textH);
        HUnlock(scrapText);
    }

    PushUndoSnapshot();

    ZeroScrap();
    HLock(scrapText);
    PutScrap(GetHandleSize(scrapText), 'TEXT', *scrapText);
    HUnlock(scrapText);
    DisposeHandle(scrapText);

    TEDelete(gActiveTE);

    gDirty = true;
    gTypingRunActive = false;
    AdjustScrollbar();
}

void DoCopy(void)
{
    short selStart, selEnd;
    long selLen;
    Handle scrapText;

    selStart = (**gActiveTE).selStart;
    selEnd = (**gActiveTE).selEnd;
    if (selStart == selEnd)
        return;

    if (gHideMarkdown) {
        scrapText = EncodeSelectionAsMarkdown(selStart, selEnd, gActiveTE);
    } else {
        Handle textH = (**gActiveTE).hText;

        selLen = selEnd - selStart;
        scrapText = NewHandle(selLen);
        HLock(textH);
        HLock(scrapText);
        BlockMove(*textH + selStart, *scrapText, selLen);
        HUnlock(textH);
        HUnlock(scrapText);
    }

    ZeroScrap();
    HLock(scrapText);
    PutScrap(GetHandleSize(scrapText), 'TEXT', *scrapText);
    HUnlock(scrapText);
    DisposeHandle(scrapText);
}

void DoPaste(void)
{
    Handle scrapH;
    long offset;
    long len;

    scrapH = NewHandle(0);
    len = GetScrap(scrapH, 'TEXT', &offset);
    if (len <= 0) {
        DisposeHandle(scrapH);
        return;
    }

    PushUndoSnapshot();

    if (gHideMarkdown) {
        InsertMarkdownAsStyled(scrapH, len, gActiveTE);
        DisposeHandle(scrapH);
    } else {
        HLock(scrapH);
        TEInsert(*scrapH, len, gActiveTE);
        HUnlock(scrapH);
        DisposeHandle(scrapH);
    }

    gDirty = true;
    gTypingRunActive = false;
    AdjustScrollbar();
}

void DoSelectAll(void)
{
    TESetSelect(0, 32767, gActiveTE);
    gTypingRunActive = false;
}

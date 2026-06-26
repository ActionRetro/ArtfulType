#include "app.h"

static void RefreshActiveView(void)
{
    if (gHideMarkdown)
        BuildHiddenView();
    else
        ClearStyles();
}

void SetViewMode(Boolean hideMarkdown)
{
    if (hideMarkdown == gHideMarkdown)
        return;

    ClearUndoRedoStacks();
    UpdateEditMenuState();
    TEDeactivate(gActiveTE);

    if (hideMarkdown) {
        BuildHiddenView();
        gActiveTE = gHiddenTE;
    } else {
        SyncHiddenToCanonical();
        gActiveTE = gTE;
    }

    TEActivate(gActiveTE);
    gHideMarkdown = hideMarkdown;
    CheckItem(gViewMenu, iMarkdownView, !hideMarkdown);
    CheckItem(gViewMenu, iWriterView, hideMarkdown);
    UpdateMenuBarLook();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

static void WriteFile(StringPtr name, short vRefNum)
{
    short refNum;
    long count;
    Handle textH = (**gTE).hText;
    OSErr err;

    Create(name, vRefNum, 'ArtT', 'TEXT');

    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr)
        return;

    SetEOF(refNum, 0);
    count = (**gTE).teLength;
    HLock(textH);
    FSWrite(refNum, &count, *textH);
    HUnlock(textH);
    FSClose(refNum);
}

static void ReadFile(StringPtr name, short vRefNum)
{
    short refNum;
    long count;
    long eof;
    Handle textH;
    OSErr err;

    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr)
        return;

    GetEOF(refNum, &eof);
    textH = NewHandle(eof);
    HLock(textH);
    count = eof;
    FSRead(refNum, &count, *textH);
    FSClose(refNum);

    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    TEInsert(*textH, count, gTE);
    HUnlock(textH);
    DisposeHandle(textH);

    gDirty = false;
    ClearUndoRedoStacks();
    UpdateEditMenuState();
    RefreshActiveView();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

void DoStartupOpen(void)
{
    short message, count;
    AppFile theFile;

    CountAppFiles(&message, &count);
    if (count < 1 || message != appOpen)
        return;

    GetAppFiles(1, &theFile);
    BlockMove(theFile.fName, gFileName, theFile.fName[0] + 1);
    gVRefNum = theFile.vRefNum;
    gHaveFile = true;
    ReadFile(gFileName, gVRefNum);
    ClrAppFiles(1);
}

Boolean DoSaveAs(void)
{
    SFReply reply;
    Point where = {100, 100};

    if (gHideMarkdown)
        SyncHiddenToCanonical();

    SFPutFile(where, "\pSave document as:", "\pUntitled.md", NULL, &reply);
    UpdateMenuBarLook();
    if (!reply.good)
        return false;

    BlockMove(reply.fName, gFileName, reply.fName[0] + 1);
    gVRefNum = reply.vRefNum;
    gHaveFile = true;
    WriteFile(gFileName, gVRefNum);
    gDirty = false;
    return true;
}

Boolean DoSave(void)
{
    if (!gHaveFile)
        return DoSaveAs();

    if (gHideMarkdown)
        SyncHiddenToCanonical();

    WriteFile(gFileName, gVRefNum);
    gDirty = false;
    return true;
}

static short AskSaveChanges(void)
{
    short hit;

    if (gHaveFile)
        ParamText(gFileName, "\p", "\p", "\p");
    else
        ParamText("\pUntitled", "\p", "\p", "\p");

    hit = Alert(kSaveChangesAlert, NULL);
    UpdateMenuBarLook();
    return hit;
}

Boolean ConfirmDiscardChanges(void)
{
    if (!gDirty)
        return true;

    switch (AskSaveChanges()) {
        case kSaveBtn:     return DoSave();
        case kDontSaveBtn: return true;
        default:            return false;
    }
}

Boolean DoOpenFile(void)
{
    SFReply reply;
    Point where = {100, 100};
    SFTypeList types;

    types[0] = 'TEXT';

    SFGetFile(where, "\p", NULL, 1, types, NULL, &reply);
    UpdateMenuBarLook();
    if (!reply.good)
        return false;

    BlockMove(reply.fName, gFileName, reply.fName[0] + 1);
    gVRefNum = reply.vRefNum;
    gHaveFile = true;
    ReadFile(gFileName, gVRefNum);
    return true;
}

void DoNewFile(void)
{
    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    gHaveFile = false;
    gDirty = false;
    ClearUndoRedoStacks();
    UpdateEditMenuState();
    RefreshActiveView();
    AdjustScrollbar();
    InvalRect(&gWindow->portRect);
}

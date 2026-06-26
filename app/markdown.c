#include "app.h"

short AddLinkURL(const unsigned char *url)
{
    if (gLinkCount >= MAX_LINKS)
        return 0;
    gLinkCount++;
    BlockMove((Ptr) url, (Ptr) gLinkURLs[gLinkCount], url[0] + 1);
    return gLinkCount;
}

/*
    Markdown mode shows raw syntax with no visual styling at all -- just
    plain uniform text at the current zoom size. Selection is preserved
    since this gets called after Style-menu edits that already placed
    the caret somewhere meaningful.
*/
void ClearStyles(void)
{
    TextStyle ts;
    short fontNum;
    short savedStart = (**gTE).selStart;
    short savedEnd = (**gTE).selEnd;

    GetFNum("\pTimes", &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;

    TESetSelect(0, 32767, gTE);
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, gTE);

    TESetSelect(savedStart, savedEnd, gTE);
}

typedef struct {
    short start, end, kind, level;
    short linkID;
} StyleOp;

/*
    Builds gHiddenTE from gTE's canonical markdown text, stripping the
    delimiter characters themselves (**, *, `, [](), leading #s) and
    recording where the surviving text landed so styling can be applied
    afterward, in the stripped buffer's own coordinates.
*/
/*
    gTE and gHiddenTE are both bound to gWindow (a TE record draws into
    whatever GrafPort was current at TEStyleNew time, for its whole
    lifetime, regardless of which one is "active" later) -- so editing
    the *inactive* record still paints onto the window. Moving its
    viewRect off-screen for the duration of a rebuild makes those calls
    draw nothing, since drawing is clipped to viewRect every time.
*/
#define OFFSCREEN_COORD (-32000)

void SuppressDrawing(TEHandle te, Rect *saved)
{
    *saved = (**te).viewRect;
    SetRect(&(**te).viewRect, OFFSCREEN_COORD, OFFSCREEN_COORD,
            OFFSCREEN_COORD + 100, OFFSCREEN_COORD + 100);
}

void RestoreDrawing(TEHandle te, Rect *saved)
{
    (**te).viewRect = *saved;
}

void BuildHiddenView(void)
{
    Handle srcH;
    long len;
    Handle outH;
    long outLen;
    long i;
    static StyleOp ops[MAX_STYLE_OPS];
    short opCount;
    short fontNum;
    TextStyle ts;
    short k;
    Rect savedViewRect;

    /* Parsing the whole document and applying one TESetStyle call per
       styled span is, on real 68000 hardware, slow enough on a long,
       heavily-styled document to look like the app has hung. A watch
       cursor doesn't make it faster, but it stops it from looking
       broken -- the actual fix for the underlying slowness (lazy/
       incremental styling) is a much bigger, riskier change. */
    SetCursor(*GetCursor(watchCursor));

    opCount = 0;
    gLinkCount = 0;
    srcH = (**gTE).hText;
    len = (**gTE).teLength;
    outH = NewHandle(len + 1);
    outLen = 0;

    HLock(srcH);
    HLock(outH);

    i = 0;
    while (i < len) {
        if (i == 0 || (*srcH)[i - 1] == '\r') {
            short level = 0;

            while (level < 3 && i + level < len && (*srcH)[i + level] == '#')
                level++;
            if (level > 0 && i + level < len && (*srcH)[i + level] == ' ') {
                long lineStart = i + level + 1;
                long lineEnd = lineStart;
                long outStart = outLen;

                while (lineEnd < len && (*srcH)[lineEnd] != '\r') {
                    (*outH)[outLen++] = (*srcH)[lineEnd];
                    lineEnd++;
                }
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'H';
                    ops[opCount].level = level;
                    opCount++;
                }
                i = lineEnd;
                continue;
            }
        }

        if (i + 1 < len && (*srcH)[i] == '*' && (*srcH)[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < len && !((*srcH)[j] == '*' && (*srcH)[j + 1] == '*'))
                j++;
            if (j + 1 < len) {
                long outStart = outLen, m;

                for (m = i + 2; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'B';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if ((*srcH)[i] == '*') {
            long j = i + 1;

            while (j < len && (*srcH)[j] != '*')
                j++;
            if (j < len) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'I';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '`') {
            long j = i + 1;

            while (j < len && (*srcH)[j] != '`')
                j++;
            if (j < len) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'C';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < len && (*srcH)[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < len && closeBracket + 1 < len && (*srcH)[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < len && (*srcH)[closeParen] != ')')
                    closeParen++;
                if (closeParen < len) {
                    long outStart = outLen, m;
                    Str255 url;
                    long urlLen = closeParen - (closeBracket + 2);

                    for (m = i + 1; m < closeBracket; m++)
                        (*outH)[outLen++] = (*srcH)[m];
                    if (urlLen > 255) urlLen = 255;
                    url[0] = (unsigned char) urlLen;
                    BlockMove(*srcH + closeBracket + 2, url + 1, urlLen);
                    if (opCount < MAX_STYLE_OPS) {
                        ops[opCount].start = (short) outStart;
                        ops[opCount].end = (short) outLen;
                        ops[opCount].kind = 'L';
                        ops[opCount].linkID = AddLinkURL(url);
                        opCount++;
                    }
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        (*outH)[outLen++] = (*srcH)[i];
        i++;
    }

    HUnlock(srcH);
    HUnlock(outH);

    SuppressDrawing(gHiddenTE, &savedViewRect);

    TESetSelect(0, 32767, gHiddenTE);
    TEDelete(gHiddenTE);
    TEInsert(*outH, outLen, gHiddenTE);
    DisposeHandle(outH);

    GetFNum("\pTimes", &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;
    TESetSelect(0, 32767, gHiddenTE);
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, gHiddenTE);

    for (k = 0; k < opCount; k++) {
        TextStyle opStyle;

        TESetSelect(ops[k].start, ops[k].end, gHiddenTE);
        switch (ops[k].kind) {
            case 'B':
                opStyle.tsFace = bold;
                TESetStyle(doFace, &opStyle, true, gHiddenTE);
                break;
            case 'I':
                opStyle.tsFace = italic;
                TESetStyle(doFace, &opStyle, true, gHiddenTE);
                break;
            case 'C':
                GetFNum("\pMonaco", &opStyle.tsFont);
                TESetStyle(doFont, &opStyle, true, gHiddenTE);
                break;
            case 'L':
                opStyle.tsFace = underline;
                opStyle.tsColor.red = ops[k].linkID;
                opStyle.tsColor.green = 0;
                opStyle.tsColor.blue = 0;
                TESetStyle(doFace + doColor, &opStyle, true, gHiddenTE);
                break;
            case 'H':
                opStyle.tsFace = bold;
                opStyle.tsSize = CurrentFontSize() + (4 - ops[k].level) * 4;
                TESetStyle(doFace + doSize, &opStyle, true, gHiddenTE);
                break;
        }
    }

    TESetSelect(0, 0, gHiddenTE);

    RestoreDrawing(gHiddenTE, &savedViewRect);

    InitCursor();
}

/*
    Reverse direction: walks gHiddenTE's text + style runs and re-derives
    markdown delimiters, rebuilding gTE's canonical text from scratch.
    Headings are detected per-line (bold + a heading-sized run at the
    line's start); everything else is inline bold/italic/Monaco-as-code.
    Link underlines round-trip as "[text](url)" -- the url comes from
    gLinkURLs, keyed by the run's tsColor.red (see AddLinkURL above).
*/
void SyncHiddenToCanonical(void)
{
    Handle srcH;
    long len;
    Handle outH;
    long outCap;
    long outLen;
    long lineStart;
    short monacoFont;
    Rect savedViewRect;
    long urlSpace;
    short li;

    /* Same reasoning as the watch cursor in BuildHiddenView -- this is
       the reverse direction, called on save, mode switch, and (more
       frequently) at the start of every typing run via PushUndoSnapshot/
       PushRedoSnapshot, so a long, heavily-styled document can make it
       pause noticeably mid-typing too. */
    SetCursor(*GetCursor(watchCursor));

    srcH = (**gHiddenTE).hText;
    len = (**gHiddenTE).teLength;
    urlSpace = 0;
    for (li = 1; li <= gLinkCount; li++)
        urlSpace += gLinkURLs[li][0];
    outCap = len * 2 + 64 + urlSpace;
    outH = NewHandle(outCap);
    outLen = 0;

    GetFNum("\pMonaco", &monacoFont);

    HLock(srcH);
    HLock(outH);

    lineStart = 0;
    while (lineStart <= len) {
        long lineEnd = lineStart;
        short headingLevel = 0;
        Boolean isHeading = false;

        while (lineEnd < len && (*srcH)[lineEnd] != '\r')
            lineEnd++;

        if (lineEnd > lineStart) {
            TextStyle firstStyle;
            short dummyLH, dummyFA;

            TEGetStyle((short) lineStart, &firstStyle, &dummyLH, &dummyFA, gHiddenTE);
            if (firstStyle.tsFace & bold) {
                short lvl;

                for (lvl = 1; lvl <= 3; lvl++) {
                    if (firstStyle.tsSize == CurrentFontSize() + (4 - lvl) * 4) {
                        headingLevel = lvl;
                        isHeading = true;
                        break;
                    }
                }
            }
        }

        if (isHeading) {
            short k;

            for (k = 0; k < headingLevel; k++)
                (*outH)[outLen++] = '#';
            (*outH)[outLen++] = ' ';
            BlockMove(*srcH + lineStart, *outH + outLen, lineEnd - lineStart);
            outLen += (lineEnd - lineStart);
        } else {
            long i = lineStart;
            Boolean inBold = false, inItalic = false, inCode = false, inLink = false;
            Str255 curLinkURL;

            while (i <= lineEnd) {
                Boolean wantBold = false, wantItalic = false, wantCode = false, wantLink = false;
                short linkID = 0;

                if (i < lineEnd) {
                    TextStyle st;
                    short dlh, dfa;

                    TEGetStyle((short) i, &st, &dlh, &dfa, gHiddenTE);
                    wantBold = (st.tsFace & bold) != 0;
                    wantItalic = (st.tsFace & italic) != 0;
                    wantCode = (st.tsFont == monacoFont);
                    wantLink = (st.tsFace & underline) != 0;
                    linkID = st.tsColor.red;
                }

                /* Close innermost-first: code, italic, bold, then link
                   (link is the outermost wrapper, [bold link](url)). */
                if (inCode && !wantCode) { (*outH)[outLen++] = '`'; inCode = false; }
                if (inItalic && !wantItalic) { (*outH)[outLen++] = '*'; inItalic = false; }
                if (inBold && !wantBold) {
                    (*outH)[outLen++] = '*';
                    (*outH)[outLen++] = '*';
                    inBold = false;
                }
                if (inLink && !wantLink) {
                    (*outH)[outLen++] = ']';
                    (*outH)[outLen++] = '(';
                    BlockMove(curLinkURL + 1, *outH + outLen, curLinkURL[0]);
                    outLen += curLinkURL[0];
                    (*outH)[outLen++] = ')';
                    inLink = false;
                }

                if (!inLink && wantLink) {
                    (*outH)[outLen++] = '[';
                    inLink = true;
                    if (linkID >= 1 && linkID <= gLinkCount)
                        BlockMove(gLinkURLs[linkID], curLinkURL, gLinkURLs[linkID][0] + 1);
                    else
                        curLinkURL[0] = 0;
                }
                if (!inBold && wantBold) {
                    (*outH)[outLen++] = '*';
                    (*outH)[outLen++] = '*';
                    inBold = true;
                }
                if (!inItalic && wantItalic) { (*outH)[outLen++] = '*'; inItalic = true; }
                if (!inCode && wantCode) { (*outH)[outLen++] = '`'; inCode = true; }

                if (i < lineEnd)
                    (*outH)[outLen++] = (*srcH)[i];
                i++;
            }
        }

        if (lineEnd < len)
            (*outH)[outLen++] = '\r';
        lineStart = lineEnd + 1;
    }

    HUnlock(srcH);
    HUnlock(outH);

    SuppressDrawing(gTE, &savedViewRect);

    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    TEInsert(*outH, outLen, gTE);
    DisposeHandle(outH);

    ClearStyles();

    RestoreDrawing(gTE, &savedViewRect);

    InitCursor();
}

/*
    Cut/Copy/Paste go through the Scrap Manager directly (ZeroScrap/
    PutScrap/GetScrap) rather than the usual TECut/TECopy/TEPaste +
    TEToScrap/TEFromScrap pattern -- TEToScrap/TEFromScrap are declared
    in this toolchain's headers but have no actual implementation
    linked anywhere (confirmed: linker error, not a typo), so they're
    unusable here.

    Styling still survives a copy within Writer mode, just not via the
    clipboard's own (unavailable) style support: copying a Writer-mode
    selection encodes its styled runs as markdown text (the same
    inline bold/italic/code/link delimiters SyncHiddenToCanonical
    already produces for the whole document, just scoped to a range
    instead of per-line -- so headings specifically aren't
    re-derived, since they're a line-level construct that doesn't
    make sense for an arbitrary sub-range), and pasting back into
    Writer mode parses that text for the same delimiters and applies
    the corresponding styles (mirroring BuildHiddenView's inline
    parsing, again without heading handling). Plain text round-trips
    unchanged either way, including to/from other apps -- a paste
    that happens to contain a literal "*" or "`" from some other
    source will get (mis)interpreted as markdown, an accepted
    trade-off for getting styled copy/paste working at all. Markdown
    mode's copy/paste is untouched -- the selection is already raw
    markdown text, no encoding/decoding needed.
*/
Handle EncodeSelectionAsMarkdown(short start, short end, TEHandle te)
{
    Handle srcH;
    Handle outH;
    long outCap;
    long outLen;
    long urlSpace;
    short li;
    short monacoFont;
    long i;
    Boolean inBold = false, inItalic = false, inCode = false, inLink = false;
    Str255 curLinkURL;

    srcH = (**te).hText;
    urlSpace = 0;
    for (li = 1; li <= gLinkCount; li++)
        urlSpace += gLinkURLs[li][0];
    outCap = (long) (end - start) * 2 + 64 + urlSpace;
    outH = NewHandle(outCap);
    outLen = 0;

    GetFNum("\pMonaco", &monacoFont);

    HLock(srcH);
    HLock(outH);

    i = start;
    while (i <= end) {
        Boolean wantBold = false, wantItalic = false, wantCode = false, wantLink = false;
        short linkID = 0;

        if (i < end) {
            TextStyle st;
            short dlh, dfa;

            TEGetStyle((short) i, &st, &dlh, &dfa, te);
            wantBold = (st.tsFace & bold) != 0;
            wantItalic = (st.tsFace & italic) != 0;
            wantCode = (st.tsFont == monacoFont);
            wantLink = (st.tsFace & underline) != 0;
            linkID = st.tsColor.red;
        }

        if (inCode && !wantCode) { (*outH)[outLen++] = '`'; inCode = false; }
        if (inItalic && !wantItalic) { (*outH)[outLen++] = '*'; inItalic = false; }
        if (inBold && !wantBold) {
            (*outH)[outLen++] = '*';
            (*outH)[outLen++] = '*';
            inBold = false;
        }
        if (inLink && !wantLink) {
            (*outH)[outLen++] = ']';
            (*outH)[outLen++] = '(';
            BlockMove(curLinkURL + 1, *outH + outLen, curLinkURL[0]);
            outLen += curLinkURL[0];
            (*outH)[outLen++] = ')';
            inLink = false;
        }

        if (!inLink && wantLink) {
            (*outH)[outLen++] = '[';
            inLink = true;
            if (linkID >= 1 && linkID <= gLinkCount)
                BlockMove(gLinkURLs[linkID], curLinkURL, gLinkURLs[linkID][0] + 1);
            else
                curLinkURL[0] = 0;
        }
        if (!inBold && wantBold) {
            (*outH)[outLen++] = '*';
            (*outH)[outLen++] = '*';
            inBold = true;
        }
        if (!inItalic && wantItalic) { (*outH)[outLen++] = '*'; inItalic = true; }
        if (!inCode && wantCode) { (*outH)[outLen++] = '`'; inCode = true; }

        if (i < end)
            (*outH)[outLen++] = (*srcH)[i];
        i++;
    }

    HUnlock(srcH);
    HUnlock(outH);
    SetHandleSize(outH, outLen);

    return outH;
}

void InsertMarkdownAsStyled(Handle srcH, long srcLen, TEHandle te)
{
    Handle outH;
    long outLen;
    long i;
    static StyleOp ops[MAX_STYLE_OPS];
    short opCount = 0;
    short insertStart;
    short k;
    TextStyle baseStyle;
    short fontNum;

    outH = NewHandle(srcLen + 1);
    outLen = 0;

    HLock(srcH);
    HLock(outH);

    i = 0;
    while (i < srcLen) {
        if (i + 1 < srcLen && (*srcH)[i] == '*' && (*srcH)[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < srcLen && !((*srcH)[j] == '*' && (*srcH)[j + 1] == '*'))
                j++;
            if (j + 1 < srcLen) {
                long outStart = outLen, m;

                for (m = i + 2; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'B';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if ((*srcH)[i] == '*') {
            long j = i + 1;

            while (j < srcLen && (*srcH)[j] != '*')
                j++;
            if (j < srcLen) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'I';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '`') {
            long j = i + 1;

            while (j < srcLen && (*srcH)[j] != '`')
                j++;
            if (j < srcLen) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'C';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < srcLen && (*srcH)[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < srcLen && closeBracket + 1 < srcLen && (*srcH)[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < srcLen && (*srcH)[closeParen] != ')')
                    closeParen++;
                if (closeParen < srcLen) {
                    long outStart = outLen, m;
                    Str255 url;
                    long urlLen = closeParen - (closeBracket + 2);

                    for (m = i + 1; m < closeBracket; m++)
                        (*outH)[outLen++] = (*srcH)[m];
                    if (urlLen > 255) urlLen = 255;
                    url[0] = (unsigned char) urlLen;
                    BlockMove(*srcH + closeBracket + 2, url + 1, urlLen);
                    if (opCount < MAX_STYLE_OPS) {
                        ops[opCount].start = (short) outStart;
                        ops[opCount].end = (short) outLen;
                        ops[opCount].kind = 'L';
                        ops[opCount].linkID = AddLinkURL(url);
                        opCount++;
                    }
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        (*outH)[outLen++] = (*srcH)[i];
        i++;
    }

    HUnlock(srcH);
    HUnlock(outH);

    insertStart = (**te).selStart;
    TEInsert(*outH, outLen, te);
    DisposeHandle(outH);

    /* TEInsert's new text inherits whatever style was at the
       insertion point -- normalize the whole pasted range to plain
       before applying the specific ops parsed above, the same order
       BuildHiddenView uses for the same reason. */
    GetFNum("\pTimes", &fontNum);
    baseStyle.tsFont = fontNum;
    baseStyle.tsFace = normal;
    baseStyle.tsSize = CurrentFontSize();
    baseStyle.tsColor.red = baseStyle.tsColor.green = baseStyle.tsColor.blue = 0;
    TESetSelect(insertStart, (short) (insertStart + outLen), te);
    TESetStyle(doFont + doFace + doSize + doColor, &baseStyle, true, te);

    for (k = 0; k < opCount; k++) {
        TextStyle opStyle;

        TESetSelect((short) (insertStart + ops[k].start), (short) (insertStart + ops[k].end), te);
        switch (ops[k].kind) {
            case 'B':
                opStyle.tsFace = bold;
                TESetStyle(doFace, &opStyle, true, te);
                break;
            case 'I':
                opStyle.tsFace = italic;
                TESetStyle(doFace, &opStyle, true, te);
                break;
            case 'C':
                GetFNum("\pMonaco", &opStyle.tsFont);
                TESetStyle(doFont, &opStyle, true, te);
                break;
            case 'L':
                opStyle.tsFace = underline;
                opStyle.tsColor.red = ops[k].linkID;
                opStyle.tsColor.green = 0;
                opStyle.tsColor.blue = 0;
                TESetStyle(doFace + doColor, &opStyle, true, te);
                break;
        }
    }

    TESetSelect((short) (insertStart + outLen), (short) (insertStart + outLen), te);
}

void WrapSelection(char *prefix, char *suffix)
{
    short selStart, selEnd;
    long selLen, totalLen, textLen;
    short prefixLen, suffixLen;
    Handle textH;
    Handle newH;
    Boolean outerWrapped, innerWrapped;

    selStart = (**gTE).selStart;
    selEnd = (**gTE).selEnd;
    selLen = selEnd - selStart;
    textH = (**gTE).hText;
    textLen = (**gTE).teLength;

    gDirty = true;

    prefixLen = strlen(prefix);
    suffixLen = strlen(suffix);

    HLock(textH);
    outerWrapped =
        (selStart >= prefixLen) &&
        (selEnd + suffixLen <= textLen) &&
        (memcmp(*textH + selStart - prefixLen, prefix, prefixLen) == 0) &&
        (memcmp(*textH + selEnd, suffix, suffixLen) == 0);
    innerWrapped = !outerWrapped &&
        (selLen >= prefixLen + suffixLen) &&
        (memcmp(*textH + selStart, prefix, prefixLen) == 0) &&
        (memcmp(*textH + selEnd - suffixLen, suffix, suffixLen) == 0);
    HUnlock(textH);

    if (outerWrapped) {
        /* markers sit just outside the selection -- strip them (toggle off) */
        newH = NewHandle(selLen);
        HLock(newH);
        HLock(textH);
        BlockMove(*textH + selStart, *newH, selLen);
        HUnlock(textH);

        TESetSelect(selStart - prefixLen, selEnd + suffixLen, gTE);
        TEDelete(gTE);
        TEInsert(*newH, selLen, gTE);
        HUnlock(newH);
        DisposeHandle(newH);

        TESetSelect(selStart - prefixLen, selStart - prefixLen + selLen, gTE);
        return;
    }

    if (innerWrapped) {
        /* markers are part of the selection itself -- strip them (toggle off) */
        long innerLen = selLen - prefixLen - suffixLen;

        newH = NewHandle(innerLen);
        HLock(newH);
        HLock(textH);
        BlockMove(*textH + selStart + prefixLen, *newH, innerLen);
        HUnlock(textH);

        TEDelete(gTE);
        TEInsert(*newH, innerLen, gTE);
        HUnlock(newH);
        DisposeHandle(newH);

        TESetSelect(selStart, selStart + innerLen, gTE);
        return;
    }

    totalLen = prefixLen + selLen + suffixLen;
    newH = NewHandle(totalLen);
    HLock(newH);
    HLock(textH);
    BlockMove(prefix, *newH, prefixLen);
    BlockMove(*textH + selStart, *newH + prefixLen, selLen);
    BlockMove(suffix, *newH + prefixLen + selLen, suffixLen);
    HUnlock(textH);

    TEDelete(gTE);
    TEInsert(*newH, totalLen, gTE);
    HUnlock(newH);
    DisposeHandle(newH);

    TESetSelect(selStart + prefixLen, selStart + prefixLen + selLen, gTE);
}

void ApplyHeading(short level)
{
    short selStart;
    short lineStart;
    long textLen;
    Handle textH;
    char prefix[8];
    short i;
    Boolean alreadyHeading;

    gDirty = true;

    selStart = (**gTE).selStart;
    textH = (**gTE).hText;
    textLen = (**gTE).teLength;

    lineStart = selStart;
    HLock(textH);
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    HUnlock(textH);

    for (i = 0; i < level; i++)
        prefix[i] = '#';
    prefix[level] = ' ';

    HLock(textH);
    alreadyHeading =
        (lineStart + level + 1 <= textLen) &&
        (memcmp(*textH + lineStart, prefix, level + 1) == 0);
    HUnlock(textH);

    if (alreadyHeading) {
        TESetSelect(lineStart, lineStart + level + 1, gTE);
        TEDelete(gTE);
        return;
    }

    TESetSelect(lineStart, lineStart, gTE);
    TEInsert(prefix, level + 1, gTE);
}

void DoLink(void)
{
    short selStart, selEnd;
    long selLen, totalLen;
    Handle textH;
    Handle newH;
    static char mid[] = "]()";
    short midLen = 3;
    short cursorPos;

    gDirty = true;

    selStart = (**gTE).selStart;
    selEnd = (**gTE).selEnd;
    selLen = selEnd - selStart;
    textH = (**gTE).hText;

    totalLen = 1 + selLen + midLen;
    newH = NewHandle(totalLen);
    HLock(newH);
    HLock(textH);
    (*newH)[0] = '[';
    BlockMove(*textH + selStart, *newH + 1, selLen);
    BlockMove(mid, *newH + 1 + selLen, midLen);
    HUnlock(textH);

    TEDelete(gTE);
    TEInsert(*newH, totalLen, gTE);
    HUnlock(newH);
    DisposeHandle(newH);

    cursorPos = selStart + selLen + 3;
    TESetSelect(cursorPos, cursorPos, gTE);
}

/*
    Style commands while in Hide Markdown mode apply real TextStyle
    directly to gHiddenTE instead of inserting delimiter text -- there's
    no visible syntax to insert. Toggle state is read back from the
    style at the selection start.
*/
static Boolean SelectionHasFace(Style face)
{
    TextStyle ts;
    short lh, fa;

    TEGetStyle((**gHiddenTE).selStart, &ts, &lh, &fa, gHiddenTE);
    return (ts.tsFace & face) != 0;
}

void ToggleFace(Style face)
{
    TextStyle ts;

    ts.tsFace = SelectionHasFace(face) ? normal : face;
    TESetStyle(doFace, &ts, true, gHiddenTE);
}

/* Prompts for a URL; returns true and fills in `url` if OK was clicked. */
static Boolean ShowLinkURLDialog(unsigned char *url)
{
    DialogPtr dlg;
    short item;
    DialogItemType type;
    Handle itemH;
    Rect box;
    Boolean result;

    dlg = GetNewDialog(kLinkDialog, NULL, (WindowPtr) -1L);
    if (dlg == NULL)
        return false;

    SelectDialogItemText(dlg, iLinkField, 0, 32767);

    do {
        ModalDialog(NULL, &item);
    } while (item != iLinkOK && item != iLinkCancel);

    result = (item == iLinkOK);
    if (result) {
        GetDialogItem(dlg, iLinkField, &type, &itemH, &box);
        GetDialogItemText(itemH, url);
    }

    DisposeDialog(dlg);
    SetPort(gWindow);
    UpdateMenuBarLook();
    return result;
}

/*
    "Link" in Writer mode: prompts for a URL, then applies underline +
    a link ID (see AddLinkURL) to the current selection.
*/
void DoLinkHidden(void)
{
    Str255 url;

    if ((**gHiddenTE).selStart == (**gHiddenTE).selEnd)
        return;

    if (ShowLinkURLDialog(url)) {
        TextStyle ts;

        ts.tsFace = underline;
        ts.tsColor.red = AddLinkURL(url);
        ts.tsColor.green = 0;
        ts.tsColor.blue = 0;
        TESetStyle(doFace + doColor, &ts, true, gHiddenTE);
    }
}

void ToggleCode(void)
{
    TextStyle ts;
    short lh, fa;
    short monacoFont, timesFont;

    GetFNum("\pMonaco", &monacoFont);
    GetFNum("\pTimes", &timesFont);

    TEGetStyle((**gHiddenTE).selStart, &ts, &lh, &fa, gHiddenTE);
    ts.tsFont = (ts.tsFont == monacoFont) ? timesFont : monacoFont;
    TESetStyle(doFont, &ts, true, gHiddenTE);
}

void ToggleHeadingHidden(short level)
{
    short selStart;
    long lineStart, lineEnd;
    Handle textH;
    long len;
    TextStyle ts;
    short lh, fa;
    Boolean isThisLevel;

    selStart = (**gHiddenTE).selStart;
    textH = (**gHiddenTE).hText;
    len = (**gHiddenTE).teLength;

    HLock(textH);
    lineStart = selStart;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    lineEnd = lineStart;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;
    HUnlock(textH);

    TEGetStyle((short) lineStart, &ts, &lh, &fa, gHiddenTE);
    isThisLevel = (ts.tsFace & bold) && (ts.tsSize == CurrentFontSize() + (4 - level) * 4);

    TESetSelect((short) lineStart, (short) lineEnd, gHiddenTE);
    if (isThisLevel) {
        ts.tsFace = normal;
        ts.tsSize = CurrentFontSize();
    } else {
        ts.tsFace = bold;
        ts.tsSize = CurrentFontSize() + (4 - level) * 4;
    }
    TESetStyle(doFace + doSize, &ts, true, gHiddenTE);
}

/*
    Sets the style at a zero-length selection (the insertion point) --
    Style TextEdit uses this as the style for whatever gets typed next,
    which is exactly what's needed after closing a live-converted span
    so typing doesn't keep inheriting bold/italic/code indefinitely.
*/
static void SetTypingStyleNormal(short pos)
{
    TextStyle ts;
    short fontNum;

    GetFNum("\pTimes", &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentFontSize();
    TESetSelect(pos, pos, gHiddenTE);
    TESetStyle(doFont + doFace + doSize, &ts, true, gHiddenTE);
}

/*
    Live "type the markdown, get the formatting" for Writer mode: called
    after every keystroke. Looks backward from the caret for a delimiter
    pair that the just-typed character completed, and if found, strips
    both delimiters and applies the corresponding style in place.
    Strikethrough has no native classic Mac text style, so it stays
    menu-only; everything else, including links, converts live.
*/
void DetectInlineMarkdown(char justTyped)
{
    Handle textH;
    long len;
    long caret;
    long lineStart;
    long lineEnd;

    if (justTyped == '\r') {
        SetTypingStyleNormal((**gHiddenTE).selEnd);
        return;
    }

    textH = (**gHiddenTE).hText;
    len = (**gHiddenTE).teLength;
    caret = (**gHiddenTE).selEnd;

    HLock(textH);

    lineStart = caret;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;

    lineEnd = caret;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;

    if (justTyped == ' ') {
        short level = 0;
        long p = lineStart;

        while (level < 3 && p < caret - 1 && (*textH)[p] == '#') {
            level++;
            p++;
        }
        if (level > 0 && p == caret - 1) {
            TextStyle ts;

            HUnlock(textH);
            TESetSelect((short) lineStart, (short) caret, gHiddenTE);
            TEDelete(gHiddenTE);
            TESetSelect((short) lineStart, (short) lineStart, gHiddenTE);
            ts.tsFace = bold;
            ts.tsSize = CurrentFontSize() + (4 - level) * 4;
            TESetStyle(doFace + doSize, &ts, true, gHiddenTE);
            InvalidateHeightCache();
            return;
        }
    } else if (justTyped == '*') {
        if (caret >= 4 && (*textH)[caret - 2] == '*' && (*textH)[caret - 1] == '*') {
            long p = caret - 4;

            while (p >= lineStart) {
                if ((*textH)[p] == '*' && (*textH)[p + 1] == '*' && p + 2 < caret - 2) {
                    long innerStart = p + 2;
                    long innerEnd = caret - 2;
                    TextStyle ts;

                    HUnlock(textH);
                    TESetSelect((short) innerEnd, (short) caret, gHiddenTE);
                    TEDelete(gHiddenTE);
                    TESetSelect((short) p, (short) innerStart, gHiddenTE);
                    TEDelete(gHiddenTE);

                    ts.tsFace = bold;
                    TESetSelect((short) p, (short) (innerEnd - 2), gHiddenTE);
                    TESetStyle(doFace, &ts, true, gHiddenTE);
                    SetTypingStyleNormal((short) (innerEnd - 2));
                    InvalidateHeightCache();
                    return;
                }
                p--;
            }

            /* No opening ** behind the caret -- the just-typed ** may
               instead be an OPENING delimiter for a closing ** that's
               already sitting later in the line (going back to bold
               text that was typed earlier, closing delimiter first). */
            {
                long q = caret + 1;

                while (q + 1 < lineEnd) {
                    if ((*textH)[q] == '*' && (*textH)[q + 1] == '*') {
                        long innerEnd = q;
                        TextStyle ts;

                        HUnlock(textH);
                        TESetSelect((short) innerEnd, (short) (innerEnd + 2), gHiddenTE);
                        TEDelete(gHiddenTE);
                        TESetSelect((short) (caret - 2), (short) caret, gHiddenTE);
                        TEDelete(gHiddenTE);

                        ts.tsFace = bold;
                        TESetSelect((short) (caret - 2), (short) (innerEnd - 2), gHiddenTE);
                        TESetStyle(doFace, &ts, true, gHiddenTE);
                        SetTypingStyleNormal((short) (caret - 2));
                        InvalidateHeightCache();
                        return;
                    }
                    q++;
                }
            }
        } else if (caret >= 3 && (*textH)[caret - 2] != '*') {
            long p = caret - 2;

            while (p >= lineStart) {
                if ((*textH)[p] == '*' &&
                    (p == lineStart || (*textH)[p - 1] != '*') &&
                    (*textH)[p + 1] != '*' && p + 1 < caret - 1) {
                    long innerStart = p + 1;
                    long innerEnd = caret - 1;
                    TextStyle ts;

                    HUnlock(textH);
                    TESetSelect((short) innerEnd, (short) caret, gHiddenTE);
                    TEDelete(gHiddenTE);
                    TESetSelect((short) p, (short) innerStart, gHiddenTE);
                    TEDelete(gHiddenTE);

                    ts.tsFace = italic;
                    TESetSelect((short) p, (short) (innerEnd - 1), gHiddenTE);
                    TESetStyle(doFace, &ts, true, gHiddenTE);
                    SetTypingStyleNormal((short) (innerEnd - 1));
                    InvalidateHeightCache();
                    return;
                }
                p--;
            }

            /* No opening * behind the caret -- the just-typed * may
               instead be an OPENING italic delimiter for a closing *
               that's already sitting later in the line. */
            {
                long q = caret;

                while (q < lineEnd) {
                    if ((*textH)[q] == '*' &&
                        (*textH)[q - 1] != '*' &&
                        (q + 1 == lineEnd || (*textH)[q + 1] != '*') &&
                        q > caret) {
                        long innerEnd = q;
                        TextStyle ts;

                        HUnlock(textH);
                        TESetSelect((short) innerEnd, (short) (innerEnd + 1), gHiddenTE);
                        TEDelete(gHiddenTE);
                        TESetSelect((short) (caret - 1), (short) caret, gHiddenTE);
                        TEDelete(gHiddenTE);

                        ts.tsFace = italic;
                        TESetSelect((short) (caret - 1), (short) (innerEnd - 1), gHiddenTE);
                        TESetStyle(doFace, &ts, true, gHiddenTE);
                        SetTypingStyleNormal((short) (caret - 1));
                        InvalidateHeightCache();
                        return;
                    }
                    q++;
                }
            }
        }
    } else if (justTyped == '`') {
        long p = caret - 2;

        while (p >= lineStart) {
            if ((*textH)[p] == '`' && p + 1 < caret - 1) {
                long innerStart = p + 1;
                long innerEnd = caret - 1;
                TextStyle ts;

                HUnlock(textH);
                TESetSelect((short) innerEnd, (short) caret, gHiddenTE);
                TEDelete(gHiddenTE);
                TESetSelect((short) p, (short) innerStart, gHiddenTE);
                TEDelete(gHiddenTE);

                GetFNum("\pMonaco", &ts.tsFont);
                TESetSelect((short) p, (short) (innerEnd - 1), gHiddenTE);
                TESetStyle(doFont, &ts, true, gHiddenTE);
                SetTypingStyleNormal((short) (innerEnd - 1));
                InvalidateHeightCache();
                return;
            }
            p--;
        }

        /* No opening ` behind the caret -- the just-typed ` may instead
           be an OPENING code delimiter for a closing ` already sitting
           later in the line. */
        {
            long q = caret;

            while (q < lineEnd) {
                if ((*textH)[q] == '`' && q > caret) {
                    long innerEnd = q;
                    TextStyle ts;

                    HUnlock(textH);
                    TESetSelect((short) innerEnd, (short) (innerEnd + 1), gHiddenTE);
                    TEDelete(gHiddenTE);
                    TESetSelect((short) (caret - 1), (short) caret, gHiddenTE);
                    TEDelete(gHiddenTE);

                    GetFNum("\pMonaco", &ts.tsFont);
                    TESetSelect((short) (caret - 1), (short) (innerEnd - 1), gHiddenTE);
                    TESetStyle(doFont, &ts, true, gHiddenTE);
                    SetTypingStyleNormal((short) (caret - 1));
                    InvalidateHeightCache();
                    return;
                }
                q++;
            }
        }
    } else if (justTyped == ')') {
        long closeParenPos = caret - 1;
        long p = closeParenPos - 1;

        while (p >= lineStart && (*textH)[p] != '(')
            p--;

        if (p >= lineStart && p > lineStart && (*textH)[p - 1] == ']') {
            long openParenPos = p;
            long closeBracketPos = openParenPos - 1;
            long urlStart = openParenPos + 1;
            long urlLen = closeParenPos - urlStart;
            long q = closeBracketPos - 1;

            while (q >= lineStart && (*textH)[q] != '[')
                q--;

            if (q >= lineStart) {
                long openBracketPos = q;
                Str255 url;
                short linkID;
                TextStyle ts;

                if (urlLen < 0) urlLen = 0;
                if (urlLen > 255) urlLen = 255;
                url[0] = (unsigned char) urlLen;
                BlockMove(*textH + urlStart, url + 1, urlLen);

                HUnlock(textH);

                TESetSelect((short) closeBracketPos, (short) caret, gHiddenTE);
                TEDelete(gHiddenTE);
                TESetSelect((short) openBracketPos, (short) (openBracketPos + 1), gHiddenTE);
                TEDelete(gHiddenTE);

                linkID = AddLinkURL(url);

                ts.tsFace = underline;
                ts.tsColor.red = linkID;
                ts.tsColor.green = 0;
                ts.tsColor.blue = 0;
                TESetSelect((short) openBracketPos, (short) (closeBracketPos - 1), gHiddenTE);
                TESetStyle(doFace + doColor, &ts, true, gHiddenTE);
                SetTypingStyleNormal((short) (closeBracketPos - 1));
                InvalidateHeightCache();
                return;
            }
        }
    }

    HUnlock(textH);
}

/* "None" in Writer mode: just clear the applied style on the selection. */
void ClearSelectionStyleHidden(void)
{
    TextStyle ts;
    short fontNum;

    if ((**gHiddenTE).selStart == (**gHiddenTE).selEnd)
        return;

    GetFNum("\pTimes", &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, gHiddenTE);
}

/*
    "None" in Markdown mode: strips any matched markdown delimiter pairs
    that fall entirely within the selection. Delimiters that extend
    outside the selection are left alone -- to clear those,
    extend the selection to include them, or toggle the specific Style
    menu item that applied them.
*/
void ClearMarkdownInSelection(void)
{
    Handle textH;
    short selStart, selEnd;
    Handle outH;
    long outLen;
    long i;

    selStart = (**gTE).selStart;
    selEnd = (**gTE).selEnd;
    if (selStart == selEnd)
        return;

    textH = (**gTE).hText;
    outH = NewHandle(selEnd - selStart + 1);
    outLen = 0;

    HLock(textH);
    HLock(outH);

    i = selStart;
    while (i < selEnd) {
        if (i == 0 || (*textH)[i - 1] == '\r') {
            short level = 0;
            long p = i;

            while (level < 3 && p < selEnd && (*textH)[p] == '#') {
                level++;
                p++;
            }
            if (level > 0 && p < selEnd && (*textH)[p] == ' ') {
                i = p + 1;
                continue;
            }
        }

        if (i + 1 < selEnd && (*textH)[i] == '*' && (*textH)[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < selEnd && !((*textH)[j] == '*' && (*textH)[j + 1] == '*'))
                j++;
            if (j + 1 < selEnd) {
                long k;

                for (k = i + 2; k < j; k++)
                    (*outH)[outLen++] = (*textH)[k];
                i = j + 2;
                continue;
            }
        }
        if ((*textH)[i] == '*') {
            long j = i + 1;

            while (j < selEnd && (*textH)[j] != '*')
                j++;
            if (j < selEnd) {
                long k;

                for (k = i + 1; k < j; k++)
                    (*outH)[outLen++] = (*textH)[k];
                i = j + 1;
                continue;
            }
        }
        if ((*textH)[i] == '`') {
            long j = i + 1;

            while (j < selEnd && (*textH)[j] != '`')
                j++;
            if (j < selEnd) {
                long k;

                for (k = i + 1; k < j; k++)
                    (*outH)[outLen++] = (*textH)[k];
                i = j + 1;
                continue;
            }
        }
        if ((*textH)[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < selEnd && (*textH)[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < selEnd && closeBracket + 1 < selEnd && (*textH)[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < selEnd && (*textH)[closeParen] != ')')
                    closeParen++;
                if (closeParen < selEnd) {
                    long k;

                    for (k = i + 1; k < closeBracket; k++)
                        (*outH)[outLen++] = (*textH)[k];
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        (*outH)[outLen++] = (*textH)[i];
        i++;
    }

    HUnlock(textH);
    HUnlock(outH);

    TESetSelect(selStart, selEnd, gTE);
    TEDelete(gTE);
    TEInsert(*outH, outLen, gTE);
    DisposeHandle(outH);

    TESetSelect(selStart, (short) (selStart + outLen), gTE);
}

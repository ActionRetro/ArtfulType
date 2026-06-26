#include "app.h"

#define kSplashImageWidth 128
#define kSplashImageHeight 100
#define kSplashImageRowBytes (kSplashImageWidth / 8)

/*
    CopyBits below reads this bitmap's rows in word/longword-sized
    chunks internally. A plain `unsigned char[]` only guarantees
    1-byte alignment by C's own rules, so the linker is free to place
    it at an odd address -- harmless on a 68020+ (and apparently on
    Mini vMac's CPU emulation, which never caught this), but a real
    68000 raises an Address Error immediately on any misaligned
    word/long access. Confirmed live: this exact crash, at startup,
    on a real Mac Plus, on both disk images -- not a guess.
*/
static const unsigned char kSplashImageBits[kSplashImageHeight * kSplashImageRowBytes] __attribute__((aligned(4))) = {
#include "splash_image.h"
};

static pascal void DrawSplashTitle(DialogPtr dlg, short itemNo)
{
    DialogItemType type;
    Handle itemH;
    Rect box;
    short textWidth;
    Str255 s;
    BitMap image;
    Rect imageRect;

    GetDialogItem(dlg, itemNo, &type, &itemH, &box);
    SetPort(dlg);

    TextFont(0);
    TextSize(0);
    TextFace(bold);
    BlockMove("\pThe Artful Type", s, 16);
    textWidth = StringWidth(s);
    MoveTo(box.left + (box.right - box.left - textWidth) / 2, box.top + 18);
    DrawString(s);

    image.baseAddr = (Ptr)kSplashImageBits;
    image.rowBytes = kSplashImageRowBytes;
    SetRect(&image.bounds, 0, 0, kSplashImageWidth, kSplashImageHeight);
    SetRect(&imageRect, 0, 0, kSplashImageWidth, kSplashImageHeight);
    OffsetRect(&imageRect,
        box.left + (box.right - box.left - kSplashImageWidth) / 2,
        box.top + 28);
    CopyBits(&image, &((GrafPtr)dlg)->portBits, &image.bounds, &imageRect, srcCopy, NULL);

    TextFace(normal);
    TextSize(9);
    BlockMove("\pA Distraction-Free Writing Environment", s, 39);
    textWidth = StringWidth(s);
    MoveTo(box.left + (box.right - box.left - textWidth) / 2, box.top + 144);
    DrawString(s);

    BlockMove("\pby Action Retro", s, 16);
    textWidth = StringWidth(s);
    MoveTo(box.left + (box.right - box.left - textWidth) / 2, box.top + 160);
    DrawString(s);
}

void ShowSplashScreen(void)
{
    DialogPtr dlg;
    short item;
    DialogItemType type;
    Handle itemH;
    Rect box;
    Boolean done = false;

    while (!done) {
        Rect screenBounds;
        Rect dlgBounds;
        short dlgWidth, dlgHeight;
        short newLeft, newTop;

        dlg = GetNewDialog(kSplashDialog, NULL, (WindowPtr) -1L);
        if (dlg == NULL)
            return;

        /* The DLOG resource's bounds only center it on a 512x342
           compact Mac screen. Reposition here instead, based on the
           actual screen size, so it's centered on any resolution.
           The DLOG is marked invisible so this happens before the
           dialog is ever shown -- no flash at the wrong position. */
        screenBounds = qd.screenBits.bounds;
        screenBounds.top += MENU_BAR_HEIGHT;
        dlgBounds = ((GrafPtr) dlg)->portRect;
        dlgWidth = dlgBounds.right - dlgBounds.left;
        dlgHeight = dlgBounds.bottom - dlgBounds.top;
        newLeft = screenBounds.left + (screenBounds.right - screenBounds.left - dlgWidth) / 2;
        newTop = screenBounds.top + (screenBounds.bottom - screenBounds.top - dlgHeight) / 2;
        MoveWindow(dlg, newLeft, newTop, false);
        ShowWindow(dlg);

        GetDialogItem(dlg, iSplashTitle, &type, &itemH, &box);
        SetDialogItem(dlg, iSplashTitle, type, (Handle) NewUserItemUPP(DrawSplashTitle), &box);

        do {
            ModalDialog(NULL, &item);
        } while (item != iSplashNew && item != iSplashOpen);

        DisposeDialog(dlg);
        SetPort(gWindow);
        UpdateMenuBarLook();

        /* Open Document, then Cancel in the file picker -- show the splash again */
        done = (item == iSplashNew) || DoOpenFile();
    }
}

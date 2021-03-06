/** @file
 * VBoxGuestDeskbarView - Guest Additions Deskbar Tray View
 */

/*
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <errno.h>
#define DEBUG 1
#include <Alert.h>
#include <Roster.h>
#include <Debug.h>
#include <Deskbar.h>
#include <File.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <String.h>
#include <TranslationUtils.h>

#include "VBoxGuestDeskbarView.h"
#include "VBoxGuestApplication.h"

#define VIEWNAME "VBoxGuestDeskbarView"

static status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char *)our_image >= (char *)image.text
			&& (char *)our_image <= (char *)image.text + image.text_size)
			return B_OK;
	}

	return B_ERROR;
}


VBoxGuestDeskbarView::VBoxGuestDeskbarView()
	: BView(BRect(0, 0, 15, 15), VIEWNAME, B_FOLLOW_NONE,
		B_WILL_DRAW | B_NAVIGABLE),
	fIcon(NULL), fClipboardService(NULL), fDisplayService(NULL)
{
	PRINT(("%s()\n", __FUNCTION__));
	_Init();
}

VBoxGuestDeskbarView::VBoxGuestDeskbarView(BMessage *archive)
	: BView(archive),
	fIcon(NULL)
{
	PRINT(("%s()\n", __FUNCTION__));
	archive->PrintToStream();

	_Init(archive);
}

VBoxGuestDeskbarView::~VBoxGuestDeskbarView()
{
	PRINT(("%s()\n", __FUNCTION__));
	delete fIcon;
	if (fClipboardService) {
		fClipboardService->Disconnect();
		delete fClipboardService;
	}
	VbglR3Term();
}

BArchivable *VBoxGuestDeskbarView::Instantiate(BMessage *data)
{
	PRINT(("%s()\n", __FUNCTION__));
	if (!validate_instantiation(data, VIEWNAME))
		return NULL;

	return new VBoxGuestDeskbarView(data);
}

status_t VBoxGuestDeskbarView::Archive(BMessage *data, bool deep) const
{
	status_t err;
	PRINT(("%s()\n", __FUNCTION__));

	err = BView::Archive(data, false);
	if (err < B_OK)
		return err;
	data->AddString("add_on", VBOX_GUEST_APP_SIG);
	data->AddString("class", "VBoxGuestDeskbarView");
	return B_OK;
}

void VBoxGuestDeskbarView::Draw(BRect rect)
{
	SetDrawingMode(B_OP_ALPHA);
	DrawBitmap(fIcon);
}

void VBoxGuestDeskbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Parent()) {
		SetViewColor(Parent()->ViewColor());
		SetLowColor(Parent()->LowColor());
	}
	
	if (fClipboardService) { // don't repeatedly crash deskbar if vboxdev not loaded
		Looper()->AddHandler(fClipboardService);
		fClipboardService->Connect();
	}
	
	if (fDisplayService) {
		fDisplayService->Start();
	}
}

void VBoxGuestDeskbarView::DetachedFromWindow()
{
	BMessage message(B_QUIT_REQUESTED);
	fClipboardService->MessageReceived(&message);
	fDisplayService->MessageReceived(&message);
}

void VBoxGuestDeskbarView::MouseDown(BPoint point)
{
	printf("MouseDown\n");
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (Looper() != NULL && Looper()->CurrentMessage() != NULL)
		Looper()->CurrentMessage()->FindInt32("buttons", &buttons);

	BPoint where = ConvertToScreen(point);
	
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
		menu->SetAsyncAutoDestruct(true);
		menu->SetFont(be_plain_font);
		
		menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED)));
		menu->SetTargetForItems(this);
		
		menu->Go(where, true, true, true);
	}
}

void VBoxGuestDeskbarView::MessageReceived(BMessage* message)
{
	if (message->what == B_QUIT_REQUESTED)
		RemoveFromDeskbar();
	else
		BHandler::MessageReceived(message);
}

status_t VBoxGuestDeskbarView::AddToDeskbar(bool force)
{
	BDeskbar deskbar;
	status_t err;
	PRINT(("%s()\n", __FUNCTION__));

	if (force)
		RemoveFromDeskbar();
	else if (deskbar.HasItem(VIEWNAME))
		return B_OK;

	app_info info;
	err = be_app->GetAppInfo(&info);
	PRINT(("%s: be_app->GetAppInfo: 0x%08lx\n", __FUNCTION__, err));
	if (err < B_OK)
		return err;

	BPath p(&info.ref);
	PRINT(("%s: app path: '%s'\n", __FUNCTION__, p.Path()));

	return deskbar.AddItem(&info.ref);
}

status_t VBoxGuestDeskbarView::RemoveFromDeskbar()
{
	BDeskbar deskbar;
	PRINT(("%s()\n", __FUNCTION__));

	return deskbar.RemoveItem(VIEWNAME);
}

status_t VBoxGuestDeskbarView::_Init(BMessage *archive)
{
	BString toolTipText;
	toolTipText << VBOX_PRODUCT << " Guest Additions ";
	toolTipText << VBOX_VERSION_MAJOR << "." << VBOX_VERSION_MINOR << "." << VBOX_VERSION_BUILD;
	toolTipText << "r" << VBOX_SVN_REV;

	SetToolTip(toolTipText.String());
	
	image_info info;
	if (our_image(info) != B_OK)
		return B_ERROR;

	BFile file(info.name, B_READ_ONLY);
	if (file.InitCheck() < B_OK)
		return B_ERROR;

	BResources resources(&file);
	if (resources.InitCheck() < B_OK)
		return B_ERROR;

	const void* data = NULL;
	size_t size;
	//data = resources.LoadResource(B_VECTOR_ICON_TYPE,
	//	kNetworkStatusNoDevice + i, &size);
	data = resources.LoadResource('data', 400, &size);
	if (data != NULL) {
		BMemoryIO mem(data, size);
		fIcon = BTranslationUtils::GetBitmap(&mem);
	}
	
    int rc = RTR3Init();
	printf("%d\n", rc);
    if (RT_SUCCESS(rc)) {
        rc = VbglR3Init();
    }
	printf("%d\n", rc);
	if (RT_SUCCESS(rc)) {
		fClipboardService = new VBoxClipboardService();
		fDisplayService = new VBoxDisplayService();
	}
	
	return RTErrConvertToErrno(rc);
}

extern "C" BView*
instantiate_deskbar_item(void)
{
	PRINT(("%s()\n", __FUNCTION__));
	return new VBoxGuestDeskbarView();
}


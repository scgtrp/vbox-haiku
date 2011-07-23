/** @file
 * VBoxGuestDeskbarView - Guest Additions Deskbar Tray View
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
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
#include <Path.h>
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
	fIcon(NULL), fClipboardService(NULL)
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
	if (Parent()) {
		SetViewColor(Parent()->ViewColor());
		SetLowColor(Parent()->LowColor());
	}
	
	if (fClipboardService) { // don't repeatedly crash deskbar if vboxdev not loaded
		Looper()->AddHandler(fClipboardService);
		fClipboardService->Connect();
	}
}

void VBoxGuestDeskbarView::DetachedFromWindow()
{
}

void VBoxGuestDeskbarView::MouseDown(BPoint point)
{
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (Looper() != NULL && Looper()->CurrentMessage() != NULL)
		Looper()->CurrentMessage()->FindInt32("buttons", &buttons);

	BPoint where = ConvertToScreen(point);
	
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
	} else {
		BMessenger(VBOX_GUEST_APP_SIG).SendMessage(B_ABOUT_REQUESTED);
	}
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
	if (RT_SUCCESS(rc)) {
		printf("** starting clipboard service\n");
		fClipboardService = new VBoxClipboardService();
	}
	
	return RTErrConvertToErrno(rc);
}

extern "C" BView*
instantiate_deskbar_item(void)
{
	PRINT(("%s()\n", __FUNCTION__));
	return new VBoxGuestDeskbarView();
}


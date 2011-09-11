/** @file
 * VBoxDisplay - Display resizing
 */

/*
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
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

#ifndef __VBOXDISPLAY__H
#define __VBOXDISPLAY__H

#include <Handler.h>
#include <Screen.h>

class VBoxDisplayService : public BHandler {
public:
	VBoxDisplayService();
	virtual ~VBoxDisplayService();
	
	void		Start();
	
	virtual	void		MessageReceived(BMessage* message);

private:

static status_t	_ServiceThreadNub(void *_this);
	status_t	_ServiceThread();

	uint32_t	fClientId;
	thread_id	fServiceThreadID;
volatile bool	fExiting;
	BScreen		fScreen;
};


#endif /* __VBOXSERVICESHAREDCLIPLBOARD__H */

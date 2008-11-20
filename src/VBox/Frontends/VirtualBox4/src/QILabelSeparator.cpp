/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VirtualBox Qt extensions: QILabelSeparator class implementation
 */

/*
 * Copyright (C) 2008 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include "QILabelSeparator.h"
#include "VBoxGlobal.h"

/* Qt includes */
#include <QLabel>
#include <QHBoxLayout>

QILabelSeparator::QILabelSeparator (QWidget *aParent /* = NULL */, Qt::WindowFlags aFlags /* = 0 */)
    : QWidget (aParent, aFlags)
    , mLabel (NULL)
{
    init();
}

QILabelSeparator::QILabelSeparator (const QString &aText, QWidget *aParent /* = NULL */, Qt::WindowFlags aFlags /* = 0 */)
    : QWidget (aParent, aFlags)
    , mLabel (NULL)
{
    init();
    setText (aText);
}

void QILabelSeparator::setBuddy (QWidget *aBuddy)
{
    mLabel->setBuddy (aBuddy);
}

void QILabelSeparator::clear()
{
    mLabel->clear();
}

void QILabelSeparator::setText (const QString &aText)
{
    mLabel->setText (aText);
}

QString QILabelSeparator::text() const
{
    return mLabel->text();
}

void QILabelSeparator::init()
{
    mLabel = new QLabel();
    QFrame *separator = new QFrame();
    separator->setFrameShape (QFrame::HLine);
    separator->setFrameShadow (QFrame::Sunken);
    separator->setEnabled (false);
    separator->setContentsMargins (0, 0, 0, 0);
//    separator->setStyleSheet ("QFrame {border: 1px outset black; }");
    separator->setSizePolicy (QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

    QHBoxLayout *layout = new QHBoxLayout (this);
    VBoxGlobal::setLayoutMargin (layout, 0);
    layout->addWidget (mLabel);
    layout->addWidget (separator, Qt::AlignBottom);
}

#!/bin/sh
## @file
#
# VirtualBox postinstall script for Solaris.
#
# If you just installed VirtualBox using IPS/pkg(5), you should run this
# script once to avoid rebooting the system before using VirtualBox.
#

#
# Copyright (C) 2009-2010 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

if test "$1" != "--srv4"; then
    # IPS package
    echo "Checking for older & partially installed bits..."
    ISIPS="--ips"
else
    # SRv4 package
    echo "Checking for older bits..."
    ISIPS=""
fi

${BASEDIR}/opt/VirtualBox/vboxconfig.sh --preremove --fatal "$ISIPS"

if test "$?" -eq 0; then
    echo "Installing new ones..."
    ${BASEDIR}/opt/VirtualBox/vboxconfig.sh --postinstall
    rc=$?
    if test "$rc" -ne 0; then
        echo "Completed but with errors."
        rc=1
    else
        if test "$1" != "--srv4"; then
            echo "Post installation completed successfully!"
        fi
    fi
else
    echo "## ERROR!! Failed to remove older/partially installed bits."
    rc=1
fi

exit "$rc"


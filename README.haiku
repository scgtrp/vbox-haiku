Guest Additions Installation/Usage Instructions for Haiku
---------------------------------------------------------

These are temporary instructions for those who want to test the Haiku port
of the guest additions before a proper package or installer has been created.

== You will need: ==
- The contents of this repository
- A GCC4-based Haiku installation with development tools installed
	(Partial GCC2/GCC2h support is present but not yet usable.)

== To compile the guest additions: ==
export PATH=$PATH:/path/to/vbox/source/kBuild/bin/haiku.x86
./configure --build-headless --disable-python --disable-java
kmk VBOX_ONLY_ADDITIONS=1 VBOX_WITH_ADDITION_DRIVERS=1 VBOX_USE_GCC4=1 all

If the build fails complaining about 'ev' being undefined, edit
out/haiku.x86/release/revision.kmk and change 'ev' to 9001 - this is due to
the code for finding the Subversion revision not working with Git repos.

== To install: ==
# core drivers
mkdir -p ~/config/add-ons/kernel/generic/
cp out/haiku.x86/release/bin/additions/vboxguest ~/config/add-ons/kernel/generic/
mkdir -p ~/config/add-ons/kernel/drivers/dev/misc/
cp out/haiku.x86/release/bin/additions/vboxdev ~/config/add-ons/kernel/drivers/bin/
ln -s ../../bin/vboxdev ~/config/add-ons/kernel/drivers/dev/misc/

# video
mkdir -p ~/config/add-ons/accelerants/ ~/config/add-ons/kernel/drivers/dev/graphics/
cp out/haiku.x86/release/bin/additions/vboxvideo ~/config/add-ons/kernel/drivers/bin/
ln -s ../../bin/vboxvideo ~/config/add-ons/kernel/drivers/dev/graphics/
cp out/haiku.x86/release/bin/additions/vboxvideo.accelerant /boot/system/add-ons/accelerants/

# vboxsf
mkdir -p ~/config/add-ons/kernel/file_systems/
cp out/haiku.x86/release/bin/additions/vboxsf ~/config/add-ons/kernel/file_systems/

# mouse
cp out/haiku.x86/release/bin/additions/VBoxMouse ~/config/add-ons/input_server/devices/
cp out/haiku.x86/release/bin/additions/VBoxMouseFilter ~/config/add-ons/input_server/filters/

# services
mkdir -p /boot/apps/VBoxAdditions/
cp out/haiku.x86/release/bin/additions/VBoxService /boot/apps/VBoxAdditions/
cp out/haiku.x86/release/bin/additions/VBoxTray /boot/apps/VBoxAdditions/
cp out/haiku.x86/release/bin/additions/VBoxControl /boot/apps/VBoxAdditions/
ln -sf /boot/apps/VBoxAdditions/VBoxService ~/config/boot/launch

== To uninstall: ==
rm -f ~/config/add-ons/input_server/devices/VBoxMouse
rm -f ~/config/add-ons/kernel/drivers/bin/vboxdev
rm -f ~/config/add-ons/kernel/drivers/dev/misc/vboxdev
rm -f ~/config/add-ons/kernel/drivers/bin/vboxvideo
rm -f ~/config/add-ons/kernel/drivers/dev/graphics/vboxvideo
rm -f ~/config/add-ons/kernel/file_systems/vboxsf
rm -f ~/config/add-ons/kernel/generic/vboxguest
rm -f ~/config/add-ons/accelerants/vboxvideo.accelerant
rm -rf /boot/apps/VBoxAdditions

== Usage: ==
Time synchronization and guest control are handled by VBoxService - you do
not need to do anything special to enable these. See the output of
  VBoxManage help guestcontrol exec
on your host system for guest control instructions.

Clipboard sharing and automatic screen resizing are handled by VBoxTray;
load the replicant by running /boot/apps/VBoxAdditions/VBoxTray. It will
be automatically reloaded whenever Deskbar starts until you quit it by
right-clicking it and choosing 'Quit'.

To mount a shared folder, tell Haiku to mount a null filesystem of type
'vboxsf' and pass the share name as a parameter:

mount -t vboxsf -p SharedFolderFullOfLolcats /mountpoint

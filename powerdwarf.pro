#
# Power Dwarf <powerdwarf.dracolinux.org>
# Copyright (c) 2018, Ole-André Rodlie <ole.andre.rodlie@gmail.com> All rights reserved.
#
# Available under the 3-clause BSD license
# See the LICENSE file for full details
#

TEMPLATE = subdirs
CONFIG -= ordered
SUBDIRS += lib config app
OTHER_FILES += README.md

app.depends += lib
config.depends += lib
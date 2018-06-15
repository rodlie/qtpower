#
# PowerDwarf <https://github.com/rodlie/powerdwarf>
# Copyright (c) 2018, Ole-André Rodlie <ole.andre.rodlie@gmail.com> All rights reserved.
#
# Available under the 3-clause BSD license
# See the LICENSE file for full details
#

QT += dbus gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = PowerDwarf
CONFIG += install_lib
TEMPLATE = lib
SOURCES += \
    common.cpp \
    power.cpp \
    upower.cpp \
    powermanagement.cpp \
    screensaver.cpp \
    monitor.cpp \
    hotplug.cpp
HEADERS += \
    common.h \
    power.h \
    upower.h \
    powermanagement.h \
    screensaver.h \
    monitor.h \
    hotplug.h \
    def.h \
    service.h

include(../powerdwarf.pri)

CONFIG(install_lib) {
    CONFIG += create_prl no_install_prl create_pc
    target.path = $${PREFIX}/lib$${LIBSUFFIX}
    target_docs.path = $${DOCDIR}/powerdwarf-$${VERSION}
    target_docs.files = ../LICENSE ../README.md
    target_inc.path = $${PREFIX}/include/powerdwarf
    target_inc.files = \
        common.h \
        power.h \
        upower.h \
        powermanagement.h \
        screensaver.h \
        monitor.h \
        hotplug.h \
        service.h
    target_udev.path = $${UDEVDIR}/rules.d
    target_udev.files = 90-backlight.rules
    QMAKE_PKGCONFIG_NAME = $${TARGET}
    QMAKE_PKGCONFIG_DESCRIPTION = PowerDwarf
    QMAKE_PKGCONFIG_LIBDIR = $$target.path
    QMAKE_PKGCONFIG_INCDIR = $$target_inc.path
    QMAKE_PKGCONFIG_DESTDIR = pkgconfig
    INSTALLS += target target_docs target_inc target_udev
}

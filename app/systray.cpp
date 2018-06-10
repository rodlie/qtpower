/*
# Power Dwarf <powerdwarf.dracolinux.org>
# Copyright (c) 2018, Ole-André Rodlie <ole.andre.rodlie@gmail.com> All rights reserved.
#
# Available under the 3-clause BSD license
# See the LICENSE file for full details
*/

#include "systray.h"

SysTray::SysTray(QObject *parent)
    : QObject(parent)
    , tray(0)
    , man(0)
    , pm(0)
    , ss(0)
    , ht(0)
    , pd(0)
    , wasLowBattery(false)
    , lowBatteryValue(LOW_BATTERY)
    , critBatteryValue(CRITICAL_BATTERY)
    , hasService(false)
    , lidActionBattery(LID_BATTERY_DEFAULT)
    , lidActionAC(LID_AC_DEFAULT)
    , criticalAction(CRITICAL_DEFAULT)
    , autoSuspendBattery(AUTO_SLEEP_BATTERY)
    , autoSuspendAC(0)
    , timer(0)
    , timeouts(0)
    , showNotifications(true)
    , desktopSS(true)
    , desktopPM(true)
    , showBatteryPercent(true)
    , showTray(true)
    , disableLidACOnExternalMonitors(true)
    , disableLidBatteryOnExternalMonitors(true)
    , autoSuspendBatteryAction(suspendSleep)
    , autoSuspendACAction(suspendNone)
    , xscreensaver(0)
    , startupScreensaver(true)
{
    // setup tray
    tray = new QSystemTrayIcon(QIcon::fromTheme(DEFAULT_BATTERY_ICON, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON))), this);
    connect(tray, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayActivated(QSystemTrayIcon::ActivationReason)));
    if (tray->isSystemTrayAvailable()) { tray->show(); }

    // setup manager
    man = new Power(this);
    connect(man, SIGNAL(updatedDevices()), this, SLOT(checkDevices()));
    connect(man, SIGNAL(closedLid()), this, SLOT(handleClosedLid()));
    connect(man, SIGNAL(openedLid()), this, SLOT(handleOpenedLid()));
    connect(man, SIGNAL(switchedToBattery()), this, SLOT(handleOnBattery()));
    connect(man, SIGNAL(switchedToAC()), this, SLOT(handleOnAC()));

    // setup org.freedesktop.PowerManagement
    pm = new PowerManagement();
    connect(pm, SIGNAL(HasInhibitChanged(bool)), this, SLOT(handleHasInhibitChanged(bool)));
    //connect(pm, SIGNAL(update()), this, SLOT(loadSettings()));
    connect(pm, SIGNAL(newInhibit(QString,QString,quint32)), this, SLOT(handleNewInhibitPowerManagement(QString,QString,quint32)));

    // setup org.freedesktop.ScreenSaver
    ss = new ScreenSaver();
    connect(ss, SIGNAL(newInhibit(QString,QString,quint32)), this, SLOT(handleNewInhibitScreenSaver(QString,QString,quint32)));

    // setup monitor hotplug watcher
    ht = new HotPlug();
    qRegisterMetaType<QMap<QString,bool> >("QMap<QString,bool>");
    connect(ht, SIGNAL(status(QString,bool)), this, SLOT(handleDisplay(QString,bool)));
    connect(ht, SIGNAL(found(QMap<QString,bool>)), this, SLOT(handleFoundDisplays(QMap<QString,bool>)));
    ht->requestScan();

    // setup org.freedesktop.PowerDwarf
    pd = new PowerDwarf();
    connect(this, SIGNAL(updatedMonitors()), pd, SLOT(updateMonitors()));
    connect(pd, SIGNAL(update()), this, SLOT(loadSettings()));

    // setup xscreensaver
    xscreensaver = new QProcess(this);
    connect(xscreensaver, SIGNAL(finished(int)), this, SLOT(handleScrensaverFinished(int)));

    // setup timer
    timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, SIGNAL(timeout()), this, SLOT(timeout()));
    timer->start();

    // load settings and register service
    loadSettings();
    registerService();
    QTimer::singleShot(10000, this, SLOT(checkDevices()));
}

SysTray::~SysTray()
{
    xscreensaver->close();
    man->deleteLater();
    pm->deleteLater();
    ss->deleteLater();
    ht->deleteLater();
    pd->deleteLater();
}

// what to do when user clicks systray
void SysTray::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    Q_UNUSED(reason)
    /*switch(reason) {
    case QSystemTrayIcon::Context:
    case QSystemTrayIcon::Trigger:
    default:;
    }*/
    QString config = QString("%1-config").arg(qApp->applicationFilePath());
    if (!QFile::exists(config)) { return; }
    QProcess::startDetached(config);
}

void SysTray::checkDevices()
{
    if (tray->isSystemTrayAvailable() && !tray->isVisible() && showTray) { tray->show(); }
    if (!showTray && tray->isVisible()) { tray->hide(); }

    // get battery left and add tooltip
    double batteryLeft = man->batteryLeft();
    tray->setToolTip(tr("Battery at %1%").arg(batteryLeft));
    if (batteryLeft==100) { tray->setToolTip(tr("Charged")); }

    // TODO: what is user don't have battery?
    if (!man->onBattery() && man->batteryLeft()<100) { tray->setToolTip(tray->toolTip().append(tr(" (Charging)"))); }

    // draw battery systray
    drawBattery(batteryLeft);

    // critical battery?
    if (batteryLeft>0 &&
        batteryLeft<=(double)critBatteryValue &&
        man->onBattery()) { handleCritical(); }

    // Register service if not already registered
    if (!hasService) { registerService(); }
}

// what to do when user close lid
void SysTray::handleClosedLid()
{

    qDebug() << "monitors?" << monitors;
    qDebug() << "internal monitor connected?" << internalMonitorIsConnected();
    qDebug() << "has external monitor?" << externalMonitorIsConnected();

    int type = lidNone;
    if (man->onBattery()) {  // on battery
        type = lidActionBattery;
        if (disableLidBatteryOnExternalMonitors && externalMonitorIsConnected()) {
            qDebug() << "external monitor is connected, ignore lid action";
            return;
        }
    } else { // on ac
        type = lidActionAC;
        if (disableLidACOnExternalMonitors && externalMonitorIsConnected()) {
            qDebug() << "external monitor is connected, ignore lid action";
            return;
        }
    }
    switch(type) {
    case lidLock:
        man->lockScreen();
        break;
    case lidSleep:
        man->sleep();
        break;
    case lidHibernate:
        man->hibernate();
        break;
    case lidShutdown:
        man->shutdown();
        break;
    default: ;
    }
}

// what to do when user open lid
void SysTray::handleOpenedLid()
{
    qDebug() << "lid is open";
}

// do something when switched to battery power
void SysTray::handleOnBattery()
{
    if (showNotifications && tray->isVisible()) {
        tray->showMessage(tr("On Battery"), tr("Switched to battery power."));
    }
    // TODO: add brightness
}

// do something when switched to ac power
void SysTray::handleOnAC()
{
    if (showNotifications && tray->isVisible()) {
        tray->showMessage(tr("On AC"), tr("Switched to AC power."));
    }
    // TODO: add brightness
}

// load default settings
void SysTray::loadSettings()
{
    if (Common::validPowerSettings("startup_xscreensaver")) {
        startupScreensaver = Common::loadPowerSettings("startup_xscreensaver").toInt();
    }
    if (Common::validPowerSettings("suspend_battery_timeout")) {
        autoSuspendBattery = Common::loadPowerSettings("suspend_battery_timeout").toInt();
    }
    if (Common::validPowerSettings("suspend_ac_timeout")) {
        autoSuspendAC = Common::loadPowerSettings("suspend_ac_timeout").toInt();
    }
    if (Common::validPowerSettings("suspend_battery_action")) {
        autoSuspendBatteryAction = Common::loadPowerSettings("suspend_battery_action").toInt();
    }
    if (Common::validPowerSettings("suspend_ac_action")) {
        autoSuspendACAction = Common::loadPowerSettings("suspend_ac_action").toInt();
    }
    if (Common::validPowerSettings("lowBattery")) {
        lowBatteryValue = Common::loadPowerSettings("lowBattery").toInt();
    }
    if (Common::validPowerSettings("criticalBattery")) {
        critBatteryValue = Common::loadPowerSettings("criticalBattery").toInt();
    }
    if (Common::validPowerSettings("lidBattery")) {
        lidActionBattery = Common::loadPowerSettings("lidBattery").toInt();
    }
    if (Common::validPowerSettings("lidAC")) {
        lidActionAC = Common::loadPowerSettings("lidAC").toInt();
    }
    if (Common::validPowerSettings("criticalAction")) {
        criticalAction = Common::loadPowerSettings("criticalAction").toInt();
    }
    if (Common::validPowerSettings("desktop_ss")) {
        desktopSS = Common::loadPowerSettings("desktop_ss").toBool();
    }
    if (Common::validPowerSettings("desktop_pm")) {
        desktopPM = Common::loadPowerSettings("desktop_pm").toBool();
    }
    if (Common::validPowerSettings("tray_notify")) {
        showNotifications = Common::loadPowerSettings("tray_notify").toBool();
    }
    if (Common::validPowerSettings("show_battery_percent")) {
        showBatteryPercent = Common::loadPowerSettings("show_battery_percent").toBool();
    }
    if (Common::validPowerSettings("show_tray")) {
        showTray = Common::loadPowerSettings("show_tray").toBool();
    }
    if (Common::validPowerSettings("disable_lid_action_battery_external_monitor")) {
        disableLidBatteryOnExternalMonitors = Common::loadPowerSettings("disable_lid_action_battery_external_monitor").toBool();
    }
    if (Common::validPowerSettings("disable_lid_action_ac_external_monitor")) {
        disableLidACOnExternalMonitors = Common::loadPowerSettings("disable_lid_action_ac_external_monitor").toBool();
    }

    qDebug() << "====> powerdwarf settings:";
    qDebug() << "startup_xscreensaver" << startupScreensaver;
    qDebug() << "disable_lid_action_ac_external_monitor" << disableLidACOnExternalMonitors;
    qDebug() << "disable_lid_action_battery_external_monitor" << disableLidBatteryOnExternalMonitors;
    qDebug() << "show_tray" << showTray;
    qDebug() << "battery_percent" << showBatteryPercent;
    qDebug() << "tray_notify" << showNotifications;
    qDebug() << "desktop_ss" << desktopSS;
    qDebug() << "desktop_pm" << desktopPM;
    qDebug() << "suspend_battery_timeout" << autoSuspendBattery;
    qDebug() << "suspend_ac_timeout" << autoSuspendAC;
    qDebug() << "suspend_battery_action" << autoSuspendBatteryAction;
    qDebug() << "suspend_ac_action" << autoSuspendACAction;
    qDebug() << "low battery setting" << lowBatteryValue;
    qDebug() << "critical battery setting" << critBatteryValue;
    qDebug() << "lid_battery" << lidActionBattery;
    qDebug() << "lid_ac" << lidActionAC;
    qDebug() << "critical action" << criticalAction;

    if (startupScreensaver && !xscreensaver->isReadable()) {
        qDebug() << "run xscreensaver";
        xscreensaver->start(XSCREENSAVER_RUN);
    }
}

// register session services
void SysTray::registerService()
{
    if (hasService) { return; }
    if (!QDBusConnection::sessionBus().isConnected()) {
        qWarning("Cannot connect to D-Bus.");
        return;
    }
    if (desktopPM) {
    if (!QDBusConnection::sessionBus().registerService(PM_SERVICE)) {
        qWarning() << QDBusConnection::sessionBus().lastError().message();
        return;
    }
        if (!QDBusConnection::sessionBus().registerObject(PM_PATH, pm, /*QDBusConnection::ExportAllContents*/QDBusConnection::ExportAllSlots)) {
        qWarning() << QDBusConnection::sessionBus().lastError().message();
        return;
    }
        qDebug() << "Enabled org.freedesktop.PowerManagement";
    }
    if (desktopSS) {
        if (!QDBusConnection::sessionBus().registerService(SS_SERVICE)) {
            qWarning() << QDBusConnection::sessionBus().lastError().message();
            return;
        }
        if (!QDBusConnection::sessionBus().registerObject(SS_PATH, ss, /*QDBusConnection::ExportAllContents*/QDBusConnection::ExportAllSlots)) {
            qWarning() << QDBusConnection::sessionBus().lastError().message();
            return;
        }
        qDebug() << "Enabled org.freedesktop.ScreenSaver";
    }
    if (!QDBusConnection::sessionBus().registerService(PD_SERVICE)) {
        qWarning() << QDBusConnection::sessionBus().lastError().message();
        return;
    }
    if (!QDBusConnection::sessionBus().registerObject(PD_PATH, pd, QDBusConnection::ExportAllContents)) {
        qWarning() << QDBusConnection::sessionBus().lastError().message();
        return;
    }
    qDebug() << "Enabled org.freedesktop.PowerDwarf";
    hasService = true;
}

// dbus session inhibit status handler
void SysTray::handleHasInhibitChanged(bool has_inhibit)
{
    qDebug() << "HasInhibitChanged?" << has_inhibit;
    if (has_inhibit) { resetTimer(); }
}

// handle critical battery
void SysTray::handleCritical()
{
    qDebug() << "critical battery level, action?" << criticalAction;
    switch(criticalAction) {
    case criticalHibernate:
        man->hibernate();
        break;
    case criticalShutdown:
        man->shutdown();
        break;
    default: ;
    }
}

// draw battery tray icon
void SysTray::drawBattery(double left)
{
    if (!showTray && tray->isVisible()) {
        tray->hide();
        return;
    }
    if (tray->isSystemTrayAvailable() && !tray->isVisible() && showTray) { tray->show(); }

    QIcon icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON)));
    if (left<=(double)lowBatteryValue && man->onBattery()) {
        icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_LOW, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_LOW)));
        if (!wasLowBattery) { tray->showMessage(tr("Low Battery!"), tr("You battery is almost empty, please consider connecting your computer to a power supply.")); }
        wasLowBattery = true;
    } else {
        wasLowBattery = false;
        if (left<=(double)lowBatteryValue) { // low (on ac)
            qDebug() << "low on ac";
            icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_LOW_AC, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_LOW_AC)));
        } else if (left<=critBatteryValue) { // critical
            qDebug() << "critical";
            if (man->onBattery()) {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_CRIT, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_CRIT)));
            } else {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_CRIT_AC, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_CRIT_AC)));
            }
        } else if (left>(double)lowBatteryValue && left<90) { // good
            qDebug() << "good";
            if (man->onBattery()) {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_GOOD, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_GOOD)));
            } else {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_GOOD_AC, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_GOOD_AC)));
            }
        } else if (left>=90 && left<99) { // almost full
            qDebug() << "almost full";
            if (man->onBattery()) {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_FULL, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_FULL)));
            } else {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_FULL_AC, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_FULL_AC)));
            }
        } else if(left>=99) { // full
            qDebug() << "full";
            if (man->onBattery()) {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_FULL, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_FULL)));
            } else {
                icon = QIcon::fromTheme(DEFAULT_BATTERY_ICON_CHARGED, QIcon(QString(":/icons/%1.png").arg(DEFAULT_BATTERY_ICON_CHARGED)));
            }
        } else {
            qDebug() << "something else";
            // TODO
        }
    }

    if (left > 99 || left == 0 || !man->onBattery() || !showBatteryPercent) {
        tray->setIcon(icon);
        return;
    }

    QPixmap pixmap = icon.pixmap(QSize(24, 24));
    QPainter painter(&pixmap);
    painter.setPen(QColor(Qt::black));
    painter.drawText(pixmap.rect().adjusted(1, 1, 1, 1), Qt::AlignCenter, QString("%1").arg(left));
    painter.setPen(QColor(Qt::white));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString("%1").arg(left));
    painter.setPen(QColor(Qt::transparent));
    tray->setIcon(pixmap);
}

// timeout, check if idle
// timeouts and xss must be >= user value and service has to be empty before suspend
void SysTray::timeout()
{
    if (!showTray && tray->isVisible()) { tray->hide(); }
    if (tray->isSystemTrayAvailable() && !tray->isVisible() && showTray) { tray->show(); }

    qDebug() << "timeout?" << timeouts;
    qDebug() << "XSS?" << xIdle();
    qDebug() << "inhibit?" << pm->HasInhibit();

    int autoSuspend = 0;
    int autoSuspendAction = suspendNone;
    if (man->onBattery()) {
        autoSuspend = autoSuspendBattery;
        autoSuspendAction = autoSuspendBatteryAction;
    }
    else {
        autoSuspend = autoSuspendAC;
        autoSuspendAction = autoSuspendACAction;
    }

    bool doSuspend = false;
    if (autoSuspend>0 &&
        timeouts>=autoSuspend &&
        xIdle()>=autoSuspend &&
        !pm->HasInhibit()) { doSuspend = true; }
    if (!doSuspend) { timeouts++; }
    else {
        timeouts = 0;
        qDebug() << "auto suspend activated" << autoSuspendAction;
        switch (autoSuspendAction) {
        case suspendSleep:
            man->sleep();
            break;
        case suspendHibernate:
            man->hibernate();
            break;
        case suspendShutdown:
            man->shutdown();
            break;
        default: break;
        }
    }
}

// get user idle time
int SysTray::xIdle()
{
    long idle = 0;
    Display *display = XOpenDisplay(0);
    if (display != 0) {
        XScreenSaverInfo *info = XScreenSaverAllocInfo();
        XScreenSaverQueryInfo(display, DefaultRootWindow(display), info);
        if (info) {
            idle = info->idle;
            XFree(info);
        }
    }
    XCloseDisplay(display);
    int hours = idle/(1000*60*60);
    int minutes = (idle-(hours*1000*60*60))/(1000*60);
    return minutes;
}

// reset the idle timer
void SysTray::resetTimer()
{
    timeouts = 0;
}

// handle connected and disconnected monitors
// Uses xrandr to turn on/off
void SysTray::handleDisplay(QString display, bool connected)
{
    qDebug() << display << connected;
    if (monitors[display] == connected) { return; }

    bool wasConnected = monitors[display];
    monitors[display] = connected;
    if (wasConnected && !connected) {
        // Turn off monitor using xrandr when disconnected.
        qDebug() << "remove screen" << display;
        QProcess::startDetached(QString(TURN_OFF_MONITOR).arg(display));
    } else if (!wasConnected && connected) {
        // Turn on monitor using xrandr when connected
        qDebug() << "add screen" << display;
        QString turnOn = Monitor::turnOnMonitorUsingXrandr(display);
        qDebug() << "running xrandr" << turnOn;
        QProcess::startDetached(turnOn);
        //TODO add detect for lumina
        //QProcess::startDetached(LUMINA_XCONFIG);
    }
}

// update monitor list
void SysTray::handleFoundDisplays(QMap<QString, bool> displays)
{
    qDebug() << displays;
    monitors = displays;
    emit updatedMonitors();
}

// is "internal" monitor connected? Anything starting with LVDS is ok.
bool SysTray::internalMonitorIsConnected()
{
    QMapIterator<QString, bool> i(monitors);
    while (i.hasNext()) {
        i.next();
        if (i.key().startsWith(INTERNAL_MONITOR)) {
            qDebug() << "internal monitor connected?" << i.key() << i.value();
            return i.value();
        }
    }
    return false;
}

// is "external" monitor(s) connected? This means anything not LVDS is ok.
bool SysTray::externalMonitorIsConnected()
{
    QMapIterator<QString, bool> i(monitors);
    while (i.hasNext()) {
        i.next();
        if (!i.key().startsWith(INTERNAL_MONITOR) && !i.key().startsWith(VIRTUAL_MONITOR)) {
            qDebug() << "external monitor connected?" << i.key() << i.value();
            if (i.value()) { return true; }
        }
    }
    return false;
}

// handle new inhibits
void SysTray::handleNewInhibitScreenSaver(QString application, QString reason, quint32 cookie)
{
    qDebug() << "new screensaver inhibit" << application << reason << cookie;
}

void SysTray::handleNewInhibitPowerManagement(QString application, QString reason, quint32 cookie)
{
    qDebug() << "new powermanagement inhibit" << application << reason << cookie;
}

void SysTray::handleScrensaverFinished(int exitcode)
{
    qDebug() << "xscreensaver closed, was this on purpose?" << exitcode;
    /*if (startupScreensaver) {
        qDebug() << "restart xscreensaver";
        xscreensaver->start(XSCREENSAVER_RUN);
    }*/
}
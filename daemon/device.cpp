#include "device.h"

#include <KSharedPtr>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KStandardDirs>
#include <KPluginSelector>
#include <KServiceTypeTrader>
#include <KPluginInfo>

#include <QDebug>

#include "plugins/pluginloader.h"
#include "devicelinks/devicelink.h"
#include "linkproviders/linkprovider.h"
#include "networkpackage.h"

Device::Device(const QString& id, const QString& name)
{
    m_deviceId = id;
    m_deviceName = name;
    m_paired = true;
    m_knownIdentiy = true;

    //Register in bus
    QDBusConnection::sessionBus().registerObject("/modules/kdeconnect/devices/"+id, this, QDBusConnection::ExportScriptableContents | QDBusConnection::ExportAdaptors);

    reloadPlugins();
}

Device::Device(const QString& id, const QString& name, DeviceLink* link)
{
    m_deviceId = id;
    m_deviceName = name;
    m_paired = false;
    m_knownIdentiy = true;

    //Register in bus
    QDBusConnection::sessionBus().registerObject("/modules/kdeconnect/devices/"+id, this, QDBusConnection::ExportScriptableContents | QDBusConnection::ExportAdaptors);

    addLink(link);

    reloadPlugins();
}
/*
Device::Device(const QString& id, const QString& name, DeviceLink* link)
{
    m_deviceId = id;
    m_deviceName = id; //Temporary name
    m_paired = false;
    m_knownIdentiy = false;

    addLink(link);

    NetworkPackage identityRequest;
    identityRequest.setType("IDENTITY_REQUEST");
    link->sendPackage(identityRequest);

    QDBusConnection::sessionBus().registerObject("/modules/kdeconnect/Devices/"+id, this);
}
*/

void Device::reloadPlugins()
{

    qDeleteAll(m_plugins);
    m_plugins.clear();

    QString path = KStandardDirs().resourceDirs("config").first()+"kdeconnect/";
    QMap<QString,QString> pluginStates = KSharedConfig::openConfig(path + id())->group("Plugins").entryMap();

    PluginLoader* loader = PluginLoader::instance();

    //Code borrowed from KWin
    foreach (const QString& pluginName, loader->getPluginList()) {

        const QString value = pluginStates.value(pluginName + QString::fromLatin1("Enabled"), QString());
        bool enabled = (value.isNull() ? true : QVariant(value).toBool()); //Enable all plugins by default

        qDebug() << pluginName << "enabled:" << enabled;

        if (enabled) {
            PackageInterface* plugin = loader->instantiatePluginForDevice(pluginName, this);

            connect(this, SIGNAL(receivedPackage(const NetworkPackage&)),
                    plugin, SLOT(receivePackage(const NetworkPackage&)));
    //        connect(packageInterface, SIGNAL(sendPackage(const NetworkPackage&)),
    //                device, SLOT(sendPackage(const NetworkPackage&)));


            m_plugins.append(plugin);
        }
    }


}


void Device::setPair(bool b)
{
    qDebug() << "setPair" << b;
    m_paired = b;
    KSharedConfigPtr config = KSharedConfig::openConfig("kdeconnectrc");
    if (b) {
        qDebug() << name() << "paired";
        config->group("devices").group("paired").group(id()).writeEntry("name",name());
    } else {
        qDebug() << name() << "unpaired";
        config->group("devices").group("paired").deleteGroup(id());
    }
}

static bool lessThan(DeviceLink* p1, DeviceLink* p2)
{
    return p1->provider()->priority() > p2->provider()->priority();
}

void Device::addLink(DeviceLink* link)
{
    qDebug() << "Adding link to" << id() << "via" << link->provider();

    connect(link,SIGNAL(destroyed(QObject*)),this,SLOT(linkDestroyed(QObject*)));

    m_deviceLinks.append(link);

    //TODO: Somehow destroy previous device links from the same provider,
    //but if we do it here, the provider will keep a broken ref!

    connect(link, SIGNAL(receivedPackage(NetworkPackage)), this, SLOT(privateReceivedPackage(NetworkPackage)));

    qSort(m_deviceLinks.begin(),m_deviceLinks.end(),lessThan);

    if (m_deviceLinks.size() == 1) {
        emit reachableStatusChanged();
    }

}

void Device::linkDestroyed(QObject* o)
{
    removeLink(static_cast<DeviceLink*>(o));
}

void Device::removeLink(DeviceLink* link)
{
    m_deviceLinks.removeOne(link);

    qDebug() << "RemoveLink"<< m_deviceLinks.size() << "links remaining";

    if (m_deviceLinks.empty()) {
        emit reachableStatusChanged();
    }
}

bool Device::sendPackage(const NetworkPackage& np) const
{
    Q_FOREACH(DeviceLink* dl, m_deviceLinks) {
        //TODO: Actually detect if a package is received or not, now when have TCP
        //"ESTABLISHED" connections that look legit and return true when we use them,
        //but that are actually broken
        if (dl->sendPackage(np)) return true;
    }
    return false;
}

void Device::privateReceivedPackage(const NetworkPackage& np)
{
    if (np.type() == "kdeconnect.identity" && !m_knownIdentiy) {
        m_deviceName = np.get<QString>("deviceName");
    } else if (m_paired) {
        qDebug() << "package received from trusted device";
        Q_EMIT receivedPackage(np);
    } else {
        qDebug() << "device" << name() << "not trusted, ignoring package" << np.type();
    }
}

QStringList Device::availableLinks() const
{
    QStringList sl;
    Q_FOREACH(DeviceLink* dl, m_deviceLinks) {
        sl.append(dl->provider()->name());
    }
    return sl;
}

void Device::sendPing()
{
    NetworkPackage np("kdeconnect.ping");
    bool success = sendPackage(np);
    qDebug() << "sendPing:" << success;
}

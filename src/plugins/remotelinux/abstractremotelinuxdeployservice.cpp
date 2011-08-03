/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2011 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (info@qt.nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at info@qt.nokia.com.
**
**************************************************************************/
#include "abstractremotelinuxdeployservice.h"

#include "deployablefile.h"
#include "linuxdeviceconfiguration.h"

#include <qt4projectmanager/qt4buildconfiguration.h>
#include <utils/qtcassert.h>
#include <utils/ssh/sshconnection.h>
#include <utils/ssh/sshconnectionmanager.h>

#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QString>

using namespace Qt4ProjectManager;
using namespace Utils;

namespace RemoteLinux {
namespace Internal {

namespace {
typedef QPair<DeployableFile, QString> DeployablePerHost;

enum State { Inactive, SettingUpDevice, Connecting, Deploying };

// TODO: Just change these...
const char LastDeployedHostsKey[] = "Qt4ProjectManager.MaemoRunConfiguration.LastDeployedHosts";
const char LastDeployedFilesKey[] = "Qt4ProjectManager.MaemoRunConfiguration.LastDeployedFiles";
const char LastDeployedRemotePathsKey[] = "Qt4ProjectManager.MaemoRunConfiguration.LastDeployedRemotePaths";
const char LastDeployedTimesKey[] = "Qt4ProjectManager.MaemoRunConfiguration.LastDeployedTimes";

} // anonymous namespace

class AbstractRemoteLinuxDeployServicePrivate
{
public:
    AbstractRemoteLinuxDeployServicePrivate() : state(Inactive), stopRequested(false) {}

    LinuxDeviceConfiguration::ConstPtr deviceConfiguration;
    QPointer<Qt4BuildConfiguration> buildConfiguration;
    SshConnection::Ptr connection;
    State state;
    bool stopRequested;

    QHash<DeployablePerHost, QDateTime> lastDeployed;
};
} // namespace Internal

using namespace Internal;

AbstractRemoteLinuxDeployService::AbstractRemoteLinuxDeployService(QObject *parent)
    : QObject(parent), m_d(new AbstractRemoteLinuxDeployServicePrivate)
{
}

AbstractRemoteLinuxDeployService::~AbstractRemoteLinuxDeployService()
{
    delete m_d;
}

const Qt4BuildConfiguration *AbstractRemoteLinuxDeployService::qt4BuildConfiguration() const
{
    return m_d->buildConfiguration;
}

LinuxDeviceConfiguration::ConstPtr AbstractRemoteLinuxDeployService::deviceConfiguration() const
{
    return m_d->deviceConfiguration;
}

SshConnection::Ptr AbstractRemoteLinuxDeployService::connection() const
{
    return m_d->connection;
}

void AbstractRemoteLinuxDeployService::saveDeploymentTimeStamp(const DeployableFile &deployableFile)
{
    m_d->lastDeployed.insert(DeployablePerHost(deployableFile,
        deviceConfiguration()->sshParameters().host), QDateTime::currentDateTime());
}

bool AbstractRemoteLinuxDeployService::hasChangedSinceLastDeployment(const DeployableFile &deployableFile) const
{
    const QDateTime &lastDeployed = m_d->lastDeployed.value(DeployablePerHost(deployableFile,
        deviceConfiguration()->sshParameters().host));
    return !lastDeployed.isValid()
        || QFileInfo(deployableFile.localFilePath).lastModified() > lastDeployed;
}

void AbstractRemoteLinuxDeployService::setDeviceConfiguration(const LinuxDeviceConfiguration::ConstPtr &deviceConfiguration)
{
    m_d->deviceConfiguration = deviceConfiguration;
}

void AbstractRemoteLinuxDeployService::setBuildConfiguration(Qt4BuildConfiguration *bc)
{
    m_d->buildConfiguration = bc;
}

void AbstractRemoteLinuxDeployService::start()
{
    QTC_ASSERT(m_d->state == Inactive, return);

    QString errorMsg;
    if (!isDeploymentPossible(&errorMsg)) {
        emit errorMessage(errorMsg);
        emit finished();
        return;
    }

    if (!isDeploymentNecessary()) {
        emit progressMessage(tr("No deployment action necessary. Skipping."));
        emit finished();
        return;
    }

    m_d->state = SettingUpDevice;
    doDeviceSetup();
}

void AbstractRemoteLinuxDeployService::stop()
{
    if (m_d->stopRequested)
        return;

    switch (m_d->state) {
    case Inactive:
        break;
    case SettingUpDevice:
        m_d->stopRequested = true;
        stopDeviceSetup();
        break;
    case Connecting:
        setFinished();
        break;
    case Deploying:
        m_d->stopRequested = true;
        stopDeployment();
        break;
    }
}

bool AbstractRemoteLinuxDeployService::isDeploymentPossible(QString *whyNot) const
{
    if (!deviceConfiguration()) {
        if (whyNot)
            *whyNot = tr("No device configuration set.");
        return false;
    }
    return true;
}

QVariantMap AbstractRemoteLinuxDeployService::exportDeployTimes() const
{
    QVariantMap map;
    QVariantList hostList;
    QVariantList fileList;
    QVariantList remotePathList;
    QVariantList timeList;
    typedef QHash<DeployablePerHost, QDateTime>::ConstIterator DepIt;
    for (DepIt it = m_d->lastDeployed.begin(); it != m_d->lastDeployed.end(); ++it) {
        fileList << it.key().first.localFilePath;
        remotePathList << it.key().first.remoteDir;
        hostList << it.key().second;
        timeList << it.value();
    }
    map.insert(QLatin1String(LastDeployedHostsKey), hostList);
    map.insert(QLatin1String(LastDeployedFilesKey), fileList);
    map.insert(QLatin1String(LastDeployedRemotePathsKey), remotePathList);
    map.insert(QLatin1String(LastDeployedTimesKey), timeList);
    return map;
}

void AbstractRemoteLinuxDeployService::importDeployTimes(const QVariantMap &map)
{
    const QVariantList &hostList = map.value(QLatin1String(LastDeployedHostsKey)).toList();
    const QVariantList &fileList = map.value(QLatin1String(LastDeployedFilesKey)).toList();
    const QVariantList &remotePathList
        = map.value(QLatin1String(LastDeployedRemotePathsKey)).toList();
    const QVariantList &timeList = map.value(QLatin1String(LastDeployedTimesKey)).toList();
    const int elemCount
        = qMin(qMin(hostList.size(), fileList.size()),
            qMin(remotePathList.size(), timeList.size()));
    for (int i = 0; i < elemCount; ++i) {
        const DeployableFile d(fileList.at(i).toString(), remotePathList.at(i).toString());
        m_d->lastDeployed.insert(DeployablePerHost(d, hostList.at(i).toString()),
            timeList.at(i).toDateTime());
    }
}

void AbstractRemoteLinuxDeployService::handleDeviceSetupDone(bool success)
{
    QTC_ASSERT(m_d->state == SettingUpDevice, return);

    if (!success || m_d->stopRequested) {
        setFinished();
        return;
    }

    m_d->state = Connecting;
    m_d->connection = SshConnectionManager::instance().acquireConnection(m_d->deviceConfiguration->sshParameters());
    connect(m_d->connection.data(), SIGNAL(error(Utils::SshError)),
        SLOT(handleConnectionFailure()));
    if (m_d->connection->state() == SshConnection::Connected) {
        handleConnected();
    } else {
        connect(m_d->connection.data(), SIGNAL(connected()), SLOT(handleConnected()));
        emit progressMessage(tr("Connecting to device..."));
        if (m_d->connection->state() == SshConnection::Unconnected)
            m_d->connection->connectToHost();
    }
}

void AbstractRemoteLinuxDeployService::handleDeploymentDone()
{
    QTC_ASSERT(m_d->state == Deploying, return);

    setFinished();
}

void AbstractRemoteLinuxDeployService::handleConnected()
{
    QTC_ASSERT(m_d->state == Connecting, return);

    if (m_d->stopRequested) {
        setFinished();
        return;
    }

    m_d->state = Deploying;
    doDeploy();
}

void AbstractRemoteLinuxDeployService::handleConnectionFailure()
{
    switch (m_d->state) {
    case Inactive:
    case SettingUpDevice:
        qWarning("%s: Unexpected state %d.", Q_FUNC_INFO, m_d->state);
        break;
    case Connecting: {
        QString errorMsg = tr("Could not connect to host: %1").arg(m_d->connection->errorString());
        if (deviceConfiguration()->deviceType() == LinuxDeviceConfiguration::Emulator)
            errorMsg += tr("\nDid the emulator fail to start?");
        else
            errorMsg += tr("\nIs the device connected and set up for network access?");
        emit errorMessage(errorMsg);
        setFinished();
        break;
    }
    case Deploying:
        emit errorMessage(tr("Connection error: %1").arg(m_d->connection->errorString()));
        stopDeployment();
    }
}

void AbstractRemoteLinuxDeployService::setFinished()
{
    m_d->state = Inactive;
    if (m_d->connection) {
        disconnect(m_d->connection.data(), 0, this, 0);
        SshConnectionManager::instance().releaseConnection(m_d->connection);
        m_d->connection = SshConnection::Ptr();
    }
    m_d->stopRequested = false;
    emit finished();
}

} // namespace RemoteLinux
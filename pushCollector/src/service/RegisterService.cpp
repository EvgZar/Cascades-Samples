/*!
* Copyright (c) 2012 Research In Motion Limited.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "RegisterService.hpp"

#include <QtCore/QDebug>

#include <bps/bps.h>
#include <bps/deviceinfo.h>

RegisterService::RegisterService(QObject *parent)
    : QObject(parent)
    , m_reply(0)
{
    // Connect to the sslErrors signal in order to see what errors we get when connecting to the push initiator
    bool ok = connect(&m_accessManager,SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError>& )),
                      this, SLOT(onSslErrors(QNetworkReply*, const QList<QSslError>&)));
    Q_ASSERT(ok);
    Q_UNUSED(ok);
}

void RegisterService::subscribeToPushInitiator(const User& user, const QString& token)
{
    // Keep track of the current user's information so it can be stored later
    // on a success
    m_currentUser = user;

    const Configuration config = m_configurationService.configuration();

    QUrl url(config.pushInitiatorUrl() + "/subscribe");
    url.addQueryItem("appid",config.providerApplicationId());
    url.addQueryItem("address",token);
    url.addQueryItem("osversion",deviceVersion());
    url.addQueryItem("model",deviceModel());
    url.addQueryItem("username",user.userId());
    url.addQueryItem("password",user.password());
    if (config.usingPublicPushProxyGateway()) {
        url.addQueryItem("type","public");
    } else {
        url.addQueryItem("type","bds");
    }

    qDebug() << "URL: " << url;
    m_reply = m_accessManager.get(QNetworkRequest(url));

    // Connect to the reply finished signal.
    bool ok = connect(m_reply, SIGNAL(finished()), this, SLOT(httpFinished()));
    Q_ASSERT(ok);
    Q_UNUSED(ok);
}

void RegisterService::httpFinished()
{
    qDebug() << "httpFinished called";

    int code = -1;
    QString description;

    if (m_reply->error() == QNetworkReply::NoError) {
        // Load the data using the reply QIODevice
        const QString returnCode = QString::fromUtf8(m_reply->readAll());
        qDebug() << "returnCode: " << returnCode;

        if (returnCode == "rc=200") {
            m_userDAO.save(m_currentUser);
            code = 200;
        } else if (returnCode == "rc=10001") {
            code = 10001;
            description = tr("Error: The token from the create channel was null, empty, or longer than 40 characters in length.");
        } else if (returnCode == "rc=10011") {
            code = 10011;

            // Note: This error should not occur unless, for some weird reason, the OS version or device model
            // specified in the request parameter is incorrect
            description = tr("Error: The OS version or device model of the BlackBerry was invalid.");
        } else if (returnCode == "rc=10002") {
            code = 10002;
            description = tr("Error: The application ID specified in the configuration settings could not be found, or it was found to be inactive or expired.");
        } else if (returnCode == "rc=10020") {
            code = 10020;
            description = tr("Error: The subscriber ID generated by the Push Initiator (based on the username and password specified) was null or empty, "
                             "longer than 42 characters in length, or matched the 'push_all' keyword.");
        } else if (returnCode == "rc=10025") {
            code = 10025;
            description = tr("Error: The Push Initiator application has the bypass subscription flag set to true (so no subscribe is allowed).");
        } else if (returnCode == "rc=10026") {
            code = 10026;
            description = tr("Error: The username or password specified was incorrect.");
        } else if (returnCode == "rc=10027") {
            code = 10027;

            // Note: You obviously would not want to put an error description like this, but we will to assist with
            // debugging
            description = tr("Error: A CPSubscriptionFailureException was thrown by the onSubscribeSuccess method of the implementation "
                             "being used of the ContentProviderSubscriptionService interface.");
        } else if (returnCode == "rc=10028") {
            code = 10028;

            // Note: You obviously would not want to put an error description like this, but we will to assist with
            // debugging
            description = tr("Error: The type specified was null, empty, or not one of 'public' or 'bds', or invalid for the push application type.");
        } else if (returnCode == "rc=-9999") {
            code = -9999;
            description = tr("Error: General error (i.e. rc=-9999).");
        } else {
            description = tr("Error: Unknown error code: %0.").arg(returnCode);
        }
    } else {
        qDebug() << "network error";
        code = m_reply->error();
        description = m_reply->errorString();
    }

    emit piRegistrationCompleted(code, description);

    // The reply is not needed now we use deleteLater since we are in a slot.
    m_reply->deleteLater();
}

void RegisterService::onSslErrors(QNetworkReply * reply, const QList<QSslError> & errors)
{
    // Ignore all SSL errors here to be able to load from a secure address.
    // It might be a good idea to display an error message indicating that security may be compromised.
    // The errors we get are:
    // "SSL error: The issuer certificate of a locally looked up certificate could not be found"
    // "SSL error: The root CA certificate is not trusted for this purpose"
    // Seems to be a problem with how the server is set up and a known QT issue QTBUG-23625

    qDebug() << "onSslErrors called";
    reply->ignoreSslErrors(errors);
}

QString RegisterService::deviceVersion() const
{
    QString deviceVersion;

    if (bps_initialize() == BPS_SUCCESS) {
        qDebug() << "bps initialized";
        deviceinfo_details_t *deviceDetails = 0;

        if (deviceinfo_get_details(&deviceDetails) == BPS_SUCCESS) {
            deviceVersion = deviceinfo_details_get_device_os_version(deviceDetails);
            deviceinfo_free_details(&deviceDetails);
        } else {
            qDebug() << "error retrieving device details";
        }

        bps_shutdown();
    } else {
        qDebug() << "error initializing bps";
    }

    return deviceVersion;
}

QString RegisterService::deviceModel() const
{
    QString deviceModel;

    if (bps_initialize() == BPS_SUCCESS) {
        qDebug() << "bps initialized";
        deviceinfo_details_t *deviceDetails = 0;

        if (deviceinfo_get_details(&deviceDetails) == BPS_SUCCESS) {
            deviceModel = deviceinfo_details_get_hardware_id(deviceDetails);
            deviceinfo_free_details(&deviceDetails);
        } else {
            qDebug() << "error retrieving device details";
        }

        bps_shutdown();
    } else {
        qDebug() << "error initializing bps";
    }

    return deviceModel;
}
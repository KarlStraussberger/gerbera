/*MT*

    MediaTomb - http://www.mediatomb.cc/

    conn_mgr_service.h - this file is part of MediaTomb.

    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.cc>,
                       Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>

    Copyright (C) 2006-2010 Gena Batyan <bgeradz@mediatomb.cc>,
                            Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>,
                            Leonhard Wimmer <leo@mediatomb.cc>

    Copyright (C) 2016-2025 Gerbera Contributors

    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.

    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    version 2 along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

    $Id$
*/

/// \file conn_mgr_service.h
/// \brief Definition of the ConnectionManagerService class.
#ifndef __UPNP_CM_H__
#define __UPNP_CM_H__

#include "upnp_service.h"

class CdsObject;
class Context;
class Database;

/// \brief This class is responsible for the UPnP Connection Manager Service operations.
///
/// Handles subscription and action invocation requests for the Connection Manager.
class ConnectionManagerService : public UpnpService {
protected:
    /// \brief UPnP standard defined action: GetCurrentConnectionIDs()
    /// \param request Incoming ActionRequest.
    ///
    /// GetCurrentConnectionIDs(string ConnectionIDs)
    ///
    /// This is currently unsupported (returns empty string)
    void doGetCurrentConnectionIDs(ActionRequest& request) const;

    /// \brief UPnP standard defined action: GetCurrentConnectionInfo()
    /// \param request Incoming ActionRequest.
    ///
    /// GetCurrentConnectionInfo(i4 ConnectoinID, i4 RcsID, i4 AVTransportID, string ProtocolInfo,
    /// string PeerConnectionManager, i4 PeerConnectionID, string Direction, string Status)
    ///
    /// This action is currently unsupported.
    void doGetCurrentConnectionInfo(ActionRequest& request) const;

    /// \brief UPnP standard defined action: GetProtocolInfo()
    /// \param request Incoming ActionRequest.
    ///
    /// GetProtocolInfo(string Source, string Sink)
    void doGetProtocolInfo(ActionRequest& request) const;

    std::shared_ptr<Database> database;

public:
    /// \brief Constructor for the CMS, saves the service type and service id
    /// in internal variables.
    explicit ConnectionManagerService(const std::shared_ptr<Context>& context,
        const std::shared_ptr<UpnpXMLBuilder>& xmlBuilder, UpnpDevice_Handle deviceHandle);

    /// \brief Processes an incoming SubscriptionRequest.
    /// \param request Incoming SubscriptionRequest.
    ///
    /// Looks at the incoming SubscriptionRequest and accepts the subscription
    /// if everything is ok.
    bool processSubscriptionRequest(const SubscriptionRequest& request) override;

    /// \brief Sends out an event to all subscribed devices.
    /// \param sourceProtocolCsv Comma Separated Value list of protocol information
    ///
    /// Sends out an update with protocol information to all subscribed devices
    bool sendSubscriptionUpdate(const std::string& sourceProtocolCsv) override;
};

#endif // __UPNP_CM_H__

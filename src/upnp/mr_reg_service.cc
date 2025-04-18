/*MT*

    MediaTomb - http://www.mediatomb.cc/

    mr_reg_service.cc - this file is part of MediaTomb.

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

/// \file mr_reg_service.cc
#define GRB_LOG_FAC GrbLogFacility::mrregistrar

#include "mr_reg_service.h" // API

#include "action_request.h"
#include "config/config.h"
#include "config/config_val.h"
#include "context.h"
#include "exceptions.h"
#include "subscription_request.h"
#include "upnp/compat.h"
#include "upnp/upnp_common.h"
#include "upnp/xml_builder.h"
#include "util/logger.h"
#include "util/tools.h"

MRRegistrarService::MRRegistrarService(const std::shared_ptr<Context>& context,
    const std::shared_ptr<UpnpXMLBuilder>& xmlBuilder, UpnpDevice_Handle deviceHandle)
    : UpnpService(context->getConfig(), xmlBuilder, deviceHandle, UPNP_DESC_MRREG_SERVICE_ID)
{
    actionMap = {
        { "IsAuthorized", [this](ActionRequest& r) { doIsAuthorized(r); } },
        { "RegisterDevice", [this](ActionRequest& r) { doRegisterDevice(r); } },
        { "IsValidated", [this](ActionRequest& r) { doIsValidated(r); } },
    };
}

void MRRegistrarService::doIsAuthorized(ActionRequest& request) const
{
    log_debug("start");

    auto response = xmlBuilder->createResponse(request.getActionName(), UPNP_DESC_MRREG_SERVICE_TYPE);
    auto root = response->document_element();
    root.append_child("Result").append_child(pugi::node_pcdata).set_value("1");

    request.setResponse(std::move(response));
    request.setErrorCode(UPNP_E_SUCCESS);

    log_debug("end");
}

void MRRegistrarService::doRegisterDevice(ActionRequest& request) const
{
    log_debug("start");

    request.setErrorCode(UPNP_E_NOT_EXIST);

    log_debug("end");
}

void MRRegistrarService::doIsValidated(ActionRequest& request) const
{
    log_debug("start");

    auto response = xmlBuilder->createResponse(request.getActionName(), UPNP_DESC_MRREG_SERVICE_TYPE);
    auto root = response->document_element();
    root.append_child("Result").append_child(pugi::node_pcdata).set_value("1");

    request.setResponse(std::move(response));
    request.setErrorCode(UPNP_E_SUCCESS);

    log_debug("end");
}

bool MRRegistrarService::processSubscriptionRequest(const SubscriptionRequest& request)
{
    auto propset = xmlBuilder->createEventPropertySet();
    auto property = propset->document_element().first_child();
    property.append_child("ValidationRevokedUpdateID").append_child(pugi::node_pcdata).set_value("0");
    property.append_child("ValidationSucceededUpdateID").append_child(pugi::node_pcdata).set_value("0");
    property.append_child("AuthorizationDeniedUpdateID").append_child(pugi::node_pcdata).set_value("0");
    property.append_child("AuthorizationGrantedUpdateID").append_child(pugi::node_pcdata).set_value("0");

    std::string xml = UpnpXMLBuilder::printXml(*propset, "", 0);

    return UPNP_E_SUCCESS == GrbUpnpAcceptSubscription( //
               deviceHandle, config->getOption(ConfigVal::SERVER_UDN), //
               serviceID, xml, request.getSubscriptionID());
}

bool MRRegistrarService::sendSubscriptionUpdate(const std::string& sourceProtocolCsv)
{
    auto propset = xmlBuilder->createEventPropertySet();
    auto property = propset->document_element().first_child();
    property.append_child("ValidationRevokedUpdateID").append_child(pugi::node_pcdata).set_value("0");
    property.append_child("ValidationSucceededUpdateID").append_child(pugi::node_pcdata).set_value("0");
    property.append_child("AuthorizationDeniedUpdateID").append_child(pugi::node_pcdata).set_value("0");
    property.append_child("AuthorizationGrantedUpdateID").append_child(pugi::node_pcdata).set_value("0");
    property.append_child("SourceProtocolInfo").append_child(pugi::node_pcdata).set_value(sourceProtocolCsv.c_str());

    std::string xml = UpnpXMLBuilder::printXml(*propset, "", 0);
    return UPNP_E_SUCCESS == GrbUpnpNotify( //
               deviceHandle, config->getOption(ConfigVal::SERVER_UDN), //
               serviceID, xml);
}

/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "ton-server-helper.h"
#include "ns3/string.h"
#include "ns3/inet-socket-address.h"
#include "ns3/names.h"
#include "ns3/ton-server.h"


namespace ns3 {

TonServerHelper::TonServerHelper (std::string protocol, Address address)
{
  m_factory.SetTypeId ("ns3::TonServer");
  m_factory.Set ("Protocol", StringValue (protocol));
  m_factory.Set ("Local", AddressValue (address));
}

void 
TonServerHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
TonServerHelper::Install (Ptr<Node> node) const
{
  return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
TonServerHelper::Install (std::string nodeName) const
{
  Ptr<Node> node = Names::Find<Node> (nodeName);
  return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
TonServerHelper::Install (NodeContainer c) const
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      apps.Add (InstallPriv (*i));
    }

  return apps;
}

Ptr<Application>
TonServerHelper::InstallPriv (Ptr<Node> node) const
{
  Ptr<Application> app = m_factory.Create<Application> ();
  node->AddApplication (app);

  return app;
}

ApplicationContainer
TonServerHelper::InstallPriv (NodeContainer c, bool mainserver, Address address, uint16_t port)
{

  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;

      Ptr<TonServer> app = m_factory.Create<TonServer> ();
      if(mainserver)
        {
          app->SetMain();
          app->AddRemote (address, port);
        }
      node->AddApplication (app);
      apps.Add (app);

    }
  return apps;
}

ApplicationContainer
TonServerHelper::Install (Ptr<Node> node, bool mainserver, Address address, uint16_t port) 
{
  return ApplicationContainer (InstallPriv (node, mainserver, address, port));
}

ApplicationContainer
TonServerHelper::Install (std::string nodeName,bool mainserver, Address address, uint16_t port) 
{
  Ptr<Node> node = Names::Find<Node> (nodeName);
  return ApplicationContainer (InstallPriv (node, mainserver, address, port));
}

ApplicationContainer
TonServerHelper::Install (NodeContainer c, bool mainserver, Address address, uint16_t port) 
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      apps.Add (InstallPriv (*i, mainserver, address, port));
    }

  return apps;
}


} // namespace ns3

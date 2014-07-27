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
 * Author: Mohamed Amine Ismail <amine.ismail@sophia.inria.fr>
 */
#include "cdn-client-server-helper.h"
#include "ns3/cdn-server.h"
#include "ns3/cdn-client.h"
#include "ns3/udp-trace-client.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include <vector>

namespace ns3 {

CdnServerHelper::CdnServerHelper ()
{
}

CdnServerHelper::CdnServerHelper (uint16_t port)
{
  m_factory.SetTypeId (CdnServer::GetTypeId ());
  SetAttribute ("Port", UintegerValue (port));
}

void
CdnServerHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
CdnServerHelper::Install (NodeContainer c)
{

  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;

      m_server = m_factory.Create<CdnServer> ();
      node->AddApplication (m_server);
      apps.Add (m_server);

    }
  return apps;
}

ApplicationContainer
CdnServerHelper::Install (NodeContainer c, bool mainserver, std::vector<Address> address, std::vector<uint16_t> port)
{

  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;

      m_server = m_factory.Create<CdnServer> ();
      if(mainserver)
        {
          m_server->SetMain();
          NS_ASSERT(address.size()==port.size());
          for(int j=0; j<address.size(); j++)
            {
              m_server->AddRemote (address[j], port[j]);
            }
        }
      node->AddApplication (m_server);
      apps.Add (m_server);

    }
  return apps;
}



Ptr<CdnServer>
CdnServerHelper::GetServer (void)
{
  return m_server;
}

CdnClientHelper::CdnClientHelper ()
{
}

CdnClientHelper::CdnClientHelper (Address address, uint16_t port)
{
  m_factory.SetTypeId (CdnClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (address));
  SetAttribute ("RemotePort", UintegerValue (port));
}

CdnClientHelper::CdnClientHelper (Ipv4Address address, uint16_t port)
{
  m_factory.SetTypeId (CdnClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (Address(address)));
  SetAttribute ("RemotePort", UintegerValue (port));
}

CdnClientHelper::CdnClientHelper (Ipv6Address address, uint16_t port)
{
  m_factory.SetTypeId (CdnClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (Address(address)));
  SetAttribute ("RemotePort", UintegerValue (port));
}

void
CdnClientHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
CdnClientHelper::Install (NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<CdnClient> client = m_factory.Create<CdnClient> ();
      node->AddApplication (client);
      apps.Add (client);
    }
  return apps;
}

CdnTraceClientHelper::CdnTraceClientHelper ()
{
}

CdnTraceClientHelper::CdnTraceClientHelper (Address address, uint16_t port, std::string filename)
{
  m_factory.SetTypeId (UdpTraceClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (address));
  SetAttribute ("RemotePort", UintegerValue (port));
  SetAttribute ("TraceFilename", StringValue (filename));
}

CdnTraceClientHelper::CdnTraceClientHelper (Ipv4Address address, uint16_t port, std::string filename)
{
  m_factory.SetTypeId (UdpTraceClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (Address (address)));
  SetAttribute ("RemotePort", UintegerValue (port));
  SetAttribute ("TraceFilename", StringValue (filename));
}

CdnTraceClientHelper::CdnTraceClientHelper (Ipv6Address address, uint16_t port, std::string filename)
{
  m_factory.SetTypeId (UdpTraceClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (Address (address)));
  SetAttribute ("RemotePort", UintegerValue (port));
  SetAttribute ("TraceFilename", StringValue (filename));
}

void
CdnTraceClientHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
CdnTraceClientHelper::Install (NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<UdpTraceClient> client = m_factory.Create<UdpTraceClient> ();
      node->AddApplication (client);
      apps.Add (client);
    }
  return apps;
}

} // namespace ns3

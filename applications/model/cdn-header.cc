/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 INRIA
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

#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/header.h"
#include "ns3/simulator.h"
#include "cdn-header.h"
#include "ns3/ipv4-address.h"

NS_LOG_COMPONENT_DEFINE ("CdnHeader");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (CdnHeader)
  ;

CdnHeader::CdnHeader ()
  : m_syn (0)
{
  NS_LOG_FUNCTION (this);
  m_filesize=0;
  m_req_number=0;
  m_number=0;
}

  void CdnHeader::SetFileSize(uint32_t filesize)
  {
    m_filesize=filesize;
  }
  uint32_t CdnHeader::GetFileSize(void)
  {
    return m_filesize;
  }

  void CdnHeader::SetSynType(uint32_t mytype)
{
  NS_LOG_FUNCTION (this << mytype);
  m_syn=mytype;
}
  uint32_t CdnHeader::GetSynType (void) const
{
  NS_LOG_FUNCTION (this);
  return m_syn;
}

TypeId
CdnHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CdnHeader")
    .SetParent<Header> ()
    .AddConstructor<CdnHeader> ()
  ;
  return tid;
}
TypeId
CdnHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
void
CdnHeader::Print (std::ostream &os) const
{
  NS_LOG_FUNCTION (this << &os);
  os << "(syn=" << m_syn << ")";
}
uint32_t
CdnHeader::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  return (16+m_number*6);

}
  void CdnHeader::SetReqNumber(uint32_t num)
  {
    m_req_number=num;
  }
  uint32_t CdnHeader::GetReqNumber(void)
  {
    
    return m_req_number;
  }
void CdnHeader::SetPort(uint16_t port)
{
  m_port.push_back(port);
}
uint16_t CdnHeader::GetPort()
{
  uint16_t port;
  port=m_port.back();
  m_port.pop_back();
  return port;
}
uint32_t 
CdnHeader::GetNumber()
{
  return m_number;
}
void
CdnHeader::Serialize (Buffer::Iterator start) const
{
  NS_LOG_FUNCTION (this << &start);
  Buffer::Iterator i = start;
  i.WriteHtonU32 (m_syn);
  i.WriteHtonU32 (m_filesize);
  i.WriteHtonU32 (m_req_number);
  i.WriteHtonU32 (m_number);
  for(int j=0; j<m_number; j++)
    {
      i.WriteHtonU32 (m_destination[j].Get ());
      i.WriteHtonU16 (m_port[j]);
    }
}
void 
CdnHeader::SetDestination (Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  m_destination.push_back(Ipv4Address::ConvertFrom(dst));
  m_number++;
}

Address CdnHeader::GetDestination (void)
{
  NS_LOG_FUNCTION (this);
  Ipv4Address destination=m_destination.back();
  m_destination.pop_back();
  return Address(destination);
}
Address CdnHeader::PeekDestination (void)
{
  NS_LOG_FUNCTION (this);
  Ipv4Address destination=m_destination.back();
  return Address(destination);
}
uint32_t
CdnHeader::Deserialize (Buffer::Iterator start)
{
  NS_LOG_FUNCTION (this << &start);
  Buffer::Iterator i = start;
  m_syn = i.ReadNtohU32 ();
  m_filesize=i.ReadNtohU32 ();
  m_req_number=i.ReadNtohU32 ();
  m_number=i.ReadNtohU32();
  Ipv4Address destination;
  for(int j=0; j<m_number; j++)
    {
      destination.Set(i.ReadNtohU32 ());
      m_destination.push_back (destination);
      m_port.push_back(i.ReadNtohU16());
    }

  return GetSerializedSize ();
}

} // namespace ns3

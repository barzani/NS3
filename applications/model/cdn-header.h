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

#ifndef CDN_HEADER_H
#define CDN_HEADER_H

#include "ns3/header.h"
#include "cdn-rx-buffer.h"
#include "ns3/ipv4-address.h"
#include <vector>


namespace ns3 {

class CdnHeader : public Header
{
public:
  CdnHeader ();

 
  void SetSynType(uint32_t mytype);
  /**
   * \return the sequence number
   */
  uint32_t GetSynType (void) const;
  /**
   * \return the time stamp
   */

  void SetFileSize(uint32_t filesize);
  uint32_t GetFileSize(void);

  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  void SetReqNumber(uint32_t num);
  uint32_t GetReqNumber(void);
  void SetDestination (Address dst);
  Address GetDestination (void);
  void SetPort(uint16_t port);
  uint16_t GetPort();
  uint32_t GetNumber();
  Address PeekDestination (void);

private:
  uint32_t m_syn; //Type of the packet
  /*Convention will be if 0 == Regular Syn for the first server
                          1 == Syn-Ack 
                          2 == Ack (this is for pure acks only!)
                          3 == Syn to the server that is Not the first server
                          4 == This is not a syn packet!*/
  uint32_t m_filesize;
  uint32_t m_req_number;
  EventId           m_retxEvent;       //!< Retransmission event
  std::vector<Ipv4Address> m_destination; //!< destination address
  std::vector<uint16_t> m_port;
  uint32_t m_number;
};

} // namespace ns3

#endif /* SEQ_TS_HEADER_H */

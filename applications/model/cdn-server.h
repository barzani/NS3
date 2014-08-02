/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007,2008,2009 INRIA, UDCAST
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
 * Author: Amine Ismail <amine.ismail@sophia.inria.fr>
 *                      <amine.ismail@udcast.com>
 *
 */

#ifndef CDN_SERVER_H
#define CDN_SERVER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/address.h"
#include "packet-loss-counter.h"
#include "cdn-header.h"
#include "seq-ts-header.h"
#include "cdn-tx-buffer.h"
#include "cdn-rx-buffer.h"
namespace ns3 {
/**
 * \ingroup applications
 * \defgroup udpclientserver CdnClientServer
 */

/**
 * \ingroup udpclientserver
 * \class CdnServer
 * \brief A UDP server, receives UDP packets from a remote host.
 *
 * UDP packets carry a 32bits sequence number followed by a 64bits time
 * stamp in their payloads. The application uses the sequence number
 * to determine if a packet is lost, and the time stamp to compute the delay.
 */
class CdnServer : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  CdnServer ();
  virtual ~CdnServer ();
  /**
   * \brief Returns the number of lost packets
   * \return the number of lost packets
   */
  uint32_t GetLost (void) const;

  /**
   * \brief Returns the number of received packets
   * \return the number of received packets
   */
  uint32_t GetReceived (void) const;

  /**
   * \brief Returns the size of the window used for checking loss.
   * \return the size of the window used for checking loss.
   */
  uint16_t GetPacketWindowSize () const;

  /**
   * \brief Set the size of the window used for checking loss. This value should
   *  be a multiple of 8
   * \param size the size of the window used for checking loss. This value should
   *  be a multiple of 8
   */
  void SetPacketWindowSize (uint16_t size);
  /**
   * Handles any packets that are received:
   * \param: cdnhdr: meta level header.
   * \param: seqTs: subflow level header.
   * \param: AckHdr: Acknowledgement accompanying the packet.
   * \param: the address of the socket from which the packet was
   * received.
   */
  void ProcessAndHandleReceivedPacket(CdnHeader cdnhdr, SeqTsHeader seqTs, SeqTsHeader AckHdr, Ptr<Packet> packet, Address from);
  /**
   * Sets this server as the main server.
   */
  void SetMain();
  /**
   * Adds an alternative server to the list of alternatives
   * \param: ip: ip address of the alternative server
   * \param: port: port number of the alternative server
   */
  void AddRemote (Address ip, uint16_t port);
protected:
  virtual void DoDispose (void);

private:

  virtual void StartApplication (void);
  virtual void StopApplication (void);
  void ConsumeData(void);
  void ProcessAck(SeqTsHeader RecAck);
  void PopulateBuffer(void);
  Ptr<Packet> GetChunk(uint16_t reqnum, Ptr<Packet> packet);
  void DoHandleRead(Ptr<Socket> socket);


  /**
   * \brief Handle a packet reception.
   *
   * This function is called by lower layers.
   *
   * \param socket the socket the packet was received to.
   */
  void HandleRead (Ptr<Socket> socket);
  uint32_t m_sent;
  uint16_t m_port; //!< Port on which we listen for incoming packets.
  Ptr<Socket> m_socket; //!< IPv4 Socket
  Ptr<Socket> m_socket6; //!< IPv6 Socket
  uint32_t m_received; //!< Number of received packets
  PacketLossCounter m_lossCounter; //!< Lost packet counter
  uint32_t m_state; /* The state of the tcp connection
                     * 0 is for listening
                     * 1 is for Syn-Received
                     * 2 is for established
                     */
  CdnRxBuffer  m_rxBuffer;  //Reusing code from TcpRxBuffer.
  CdnTxBuffer  m_txBuffer;
  uint16_t m_chunksize;
  uint32_t m_filesize;
  bool m_ismain;
  std::vector<Address> m_peerAddress; //!< Remote peer address
  std::vector<uint16_t> m_peerPort;
  uint32_t m_speed;
  EventId         m_transmit;

};

} // namespace ns3

#endif /* UDP_SERVER_H */

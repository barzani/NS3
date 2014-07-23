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
 * Author: Behnaz Arzani <barzani@seas.upenn.edu>
 *  Based on an implementation of the udpclientserver by:
 *      
 *      Amine Ismail <amine.ismail@sophia.inria.fr>
 *                   <amine.ismail@udcast.com>
 *     
 *
 */

#ifndef CDN_CLIENT_H
#define CDN_CLIENT_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "seq-ts-header.h"
#include "cdn-rx-buffer.h"
#include "cdn-tx-buffer.h"
#include "cdn-header.h"
#include "ns3/traced-value.h"
#include "ns3/rtt-estimator.h"
#include "ns3/cdn-client-subflow.h"
#include <vector>
#include <map>

namespace ns3 {

class Socket;
class Packet;

/**
 * \ingroup cdnclientserver
 * \class CdnClient
 * \brief A Cdn client. Sends UDP packet carrying sequence number and time stamp
 *  in their payloads and load balancing across multiple cdn servers.
 *
 */
class CdnClient : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  CdnClient ();

  virtual ~CdnClient ();

  /**
   * \brief set the remote address and port
   * \param ip remote IPv4 address
   * \param port remote port
   */
  void SetRemote (Ipv4Address ip, uint16_t port);
  /**
   * \brief set the remote address and port
   * \param ip remote IPv6 address
   * \param port remote port
   */
  void SetRemote (Ipv6Address ip, uint16_t port);
  /**
   * \brief set the remote address and port
   * \param ip remote IP address
   * \param port remote port
   */
  void SetRemote (Address ip, uint16_t port);
  void PopulateBuffer (CdnHeader cdnhdr);
  void ProcessAck(Ptr<Packet> p,CdnHeader Ack);
  void SendWhatPossible(void);
  void  RcvBufOptimization(bool penal);

protected:
  virtual void DoDispose (void);

private:

  virtual void StartApplication (void);
  virtual void StopApplication (void);
  
  uint32_t SendDataPacket(Ptr<CdnClientSubflow> subflow, uint32_t seq, uint32_t maxSize);
  void EstimateRTT (const SeqTsHeader& AckHdr);
  void DupAck (const CdnHeader& t, uint32_t count);
  void NewAck (const SequenceNumber32& seq);
  void DoNewAck (const SequenceNumber32& seq);
  uint32_t ChunksInFlight ();
  void SetSubflowrWnds();
  void AddNewSubflow(CdnHeader ack);

  




  void ConsumeData(void);
  uint32_t AvailableWindow(void);
  uint32_t UnAckDataCount (void);
  uint32_t Window (void);
  void SetInitialCwnd (uint32_t cwnd);
  void  InitializeCwnd (void);
  void DoRetransmit ();
  void ReTxTimeout (void);
  void Retransmit (void);
  bool OutOfRange (uint32_t head, uint32_t tail) const;
  Ptr<CdnClientSubflow> GetNextSubflow(uint32_t *w);
  Ptr<CdnClientSubflow> GetSubflowForRetransmit(uint32_t seq);
  void OptimizingRetrans(Ptr<CdnClientSubflow> subflow);
  uint32_t m_count; //!< Maximum number of packets the application will send
  Time m_interval; //!< Packet inter-send time
  uint32_t m_size; //!< Size of the sent packet (including the SeqTsHeader)
  Ptr<Socket> m_socket; //!< the main socket!
  std::vector<Ptr<Socket> > m_socketList; // !< the list of subflows!
  std::vector<Ptr<CdnClientSubflow> > m_subflowList; // !< the list of subflowapps.
  
  Address m_peerAddress; //!< Remote peer address
  uint16_t m_peerPort; //!< Remote peer port
  EventId m_sendEvent; //!< Event to send the next packet

  uint32_t m_state; /*The state of the connection on each subflow (later you have to make this into a tupled list of some sort), 
                     * -1 closed
                     * 0 for syn-sent
                     * 1 for syn-received
                     * 2 for established 
                     */
  uint32_t m_filesize;
  uint32_t m_remfromfile;
  CdnRxBuffer  m_rxBuffer;  //Reusing code from TcpRxBuffer. 
  CdnTxBuffer  m_txBuffer;
  TracedValue<uint32_t> m_rWnd;        //!< This should be the size of the receive window at the client!!
  TracedValue<uint32_t> m_nextTxSequence; //!< Next seqnum to be sent (SND.NXT), ReTx pushes it back
  Ptr<RttEstimator> m_rtt; //!< Round trip time estimator                     
  uint32_t          m_dupAckCount;     //!< Dupack counter
  TracedValue<Time> m_lastRtt;
  TracedValue<Time> m_rto;             //!< Retransmit timeout
  TypeId m_rttTypeId; //!< The RTT Estimator TypeId
  bool   m_inFastRec;    //!< currently in fast recovery
  uint32_t               m_retxThresh;   //!< Fast Retransmit threshold
  uint16_t m_chunksize;
  TracedValue<SequenceNumber32> m_highTxMark;     //!< Highest seqno ever sent, regardless of ReTx
  bool                   m_limitedTx;    //!< perform limited transmit
  uint32_t               m_ssThresh;     //!< Slow Start Threshold
  TracedValue<uint32_t>  m_cWnd;         //!< Congestion window
  uint32_t               m_initialCWnd;  //!< Initial cWnd value
  SequenceNumber32       m_recover;      //!< Previous highest Tx seqnum for fast recovery
  uint32_t               m_cnCount;         //!< Count of remaining connection retries
  Time                   m_cnTimeout;       //!< Timeout for connection retry
  EventId           m_retxEvent;       //!< Retransmission event
  std::map< uint32_t, Ptr<CdnClientSubflow> > subflowmap;
  Time               m_lastOpt;
 
};

} // namespace ns3

#endif /* UDP_CLIENT_H */

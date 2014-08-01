/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
//
// Copyright (c) 2006 Georgia Tech Research Corporation
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// Author: Behnaz Arzani < arzanibehnaz@gmail.com>
//based on code by:
// Author: George F. Riley<riley@ece.gatech.edu>
//



#ifndef TON_APPLICATION_H
#define TON_APPLICATION_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/data-rate.h"
#include "ns3/traced-callback.h"
#include "cdn-tx-buffer.h"
#include "cdn-rx-buffer.h"
#include "cdn-header.h"
#include <vector>


namespace ns3 {

class Address;
class RandomVariableStream;
class Socket;



class TonApplication : public Application 
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  TonApplication ();

  virtual ~TonApplication();

  /**
   * \brief Set the total number of bytes to send.
   *
   * Once these bytes are sent, no packet is sent again, even in on state.
   * The value zero means that there is no limit.
   *
   * \param maxBytes the total number of bytes to send
   */
  void SetMaxBytes (uint32_t maxBytes);

  /**
   * \brief Return a pointer to associated socket.
   * \return pointer to associated socket
   */
  Ptr<Socket> GetSocket (void) const;

 /**
  * \brief Assign a fixed random variable stream number to the random variables
  * used by this model.
  *
  * \param stream first stream index to use
  * \return the number of stream indices assigned by this model
  */
  int64_t AssignStreams (int64_t stream);

  void Retransmit(Address from, Ptr<Socket> socket);

protected:
  virtual void DoDispose (void);
private:
  // inherited from Application base class.
  virtual void StartApplication (void);    // Called at time specified by Start
  virtual void StopApplication (void);     // Called at time specified by Stop



  // Event handlers
  /**
   * \brief Starts the sending process.
   */
  void StartSending (Ptr<Socket> socket);
  /**
   * \brief Start an Off period
   */
  void StopSending ();

  void PopulateBuffer(void);


  Ptr<Socket>     m_socket;       //!< Associated socket
  Address         m_peer;         //!< Peer address
  bool            m_connected;    //!< True if connected
  Ptr<RandomVariableStream>  m_onTime;       //!< rng for On Time
  Ptr<RandomVariableStream>  m_offTime;      //!< rng for Off Time
  DataRate        m_cbrRate;      //!< Rate that data is generated
  DataRate        m_cbrRateFailSafe;      //!< Rate that data is generated (check copy)
  uint32_t        m_pktSize;      //!< Size of packets
  Time            m_lastStartTime; //!< Time last packet sent

  EventId         m_startStopEvent;     //!< Event id for next start or stop event
  EventId         m_sendEvent;    //!< Event id of pending "send packet" event
  TypeId          m_tid;          //!< Type of the socket used
  

  /// Traced Callback: transmitted packets.
  TracedCallback<Ptr<const Packet> > m_txTrace;

private:

  /**
   * \brief Start transmitting requests for packets.
   */
  void Connect (Ptr<Socket> socket);
  /**
   * \brief Schedule the next Off period start
   */
  void ScheduleStopEvent ();
  /**
   * \brief Handle a Connection Succeed event
   * \param socket the connected socket
   */
  void ConnectionSucceeded (Ptr<Socket> socket);
  /**
   * \brief Handle a Connection Failed event
   * \param socket the not connected socket
   */
  void ConnectionFailed (Ptr<Socket> socket);

  void SendNextPacketTo(Address from, Ptr<Socket> socket);
  
  uint32_t SendDataPacket(Address from, Ptr<Socket> socket, uint32_t seq);

  void DoNewAck (const SequenceNumber32& seq);
  
  void NewAck (const SequenceNumber32& seq);
  void ConsumeData(void);
  void ProcessAck(Ptr<Packet> p, CdnHeader Ack);
  void AddNewSubflow(CdnHeader ack);


  void HandleRead (Ptr<Socket> socket);
  TracedValue<uint32_t> m_nextTxSequence; //!< Next seqnum to be sent (SND.NXT), ReTx pushes it back      
  TracedValue<SequenceNumber32> m_highTxMark;     //!< Highest seqno ever sent, regardless of ReTx
  CdnRxBuffer  m_rxBuffer;  //Reusing code from TcpRxBuffer. 
  CdnTxBuffer  m_txBuffer;
  uint32_t m_filesize;
  uint32_t m_remfromfile;
  std::vector<Ptr<Socket> > m_socket_list;
  Time oldReceive;
  std::map<Ptr<Socket>, Ptr<Packet> > m_halfpackets;
};

} // namespace ns3

#endif /* ONOFF_APPLICATION_H */

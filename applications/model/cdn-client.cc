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
 * Also a large portion of the code is reused from the TCP protocol stack implementation.
 */
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "cdn-client.h"
#include "seq-ts-header.h"
#include "cdn-header.h"
#include <cstdlib>
#include <cstdio>
#include "ns3/sequence-number.h"
#include "ns3/rtt-estimator.h"
#include "ns3/cdn-client-subflow.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CdnClient")
  ;
NS_OBJECT_ENSURE_REGISTERED (CdnClient)
  ;

TypeId
CdnClient::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CdnClient")
    .SetParent<Application> ()
    .AddConstructor<CdnClient> ()
    .AddAttribute ("MaxPackets",
                   "The maximum number of packets the application will send",
                   UintegerValue (100),
                   MakeUintegerAccessor (&CdnClient::m_count),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RttEstimatorType",
                   "Type of RttEstimator objects.",
                   TypeIdValue (RttMeanDeviation::GetTypeId ()),
                   MakeTypeIdAccessor (&CdnClient::m_rttTypeId),
                   MakeTypeIdChecker ())
    .AddAttribute ("Interval",
                   "The time to wait between packets", TimeValue (Seconds (1.0)),
                   MakeTimeAccessor (&CdnClient::m_interval),
                   MakeTimeChecker ())
    .AddAttribute ("RemoteAddress",
                   "The destination Address of the outbound packets",
                   AddressValue (),
                   MakeAddressAccessor (&CdnClient::m_peerAddress),
                   MakeAddressChecker ())
    .AddAttribute ("RemotePort", "The destination port of the outbound packets",
                   UintegerValue (100),
                   MakeUintegerAccessor (&CdnClient::m_peerPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PacketSize",
                   "Size of packets generated. The minimum packet size is 12 bytes which is the size of the header carrying the sequence number and the time stamp.",
                   UintegerValue (1024),
                   MakeUintegerAccessor (&CdnClient::m_size),
                   MakeUintegerChecker<uint32_t> (12,1500))
    .AddAttribute ("ReTxThreshold", "Threshold for fast retransmit",
                    UintegerValue (6),
                    MakeUintegerAccessor (&CdnClient::m_retxThresh),
                    MakeUintegerChecker<uint32_t> ())
       .AddAttribute ("LimitedTransmit", "Enable limited transmit",
		    BooleanValue (false),
		    MakeBooleanAccessor (&CdnClient::m_limitedTx),
		    MakeBooleanChecker ())
       .AddTraceSource ("CongestionWindow",
                     "The TCP connection's congestion window",
                     MakeTraceSourceAccessor (&CdnClient::m_cWnd))
  ;
  return tid;
}

CdnClient::CdnClient ()
  :m_rtt(0),
   m_inFastRec (false)
{
  NS_LOG_FUNCTION (this);
  
  m_nextTxSequence = 0;
  m_state=-1;
  m_socket = 0;
  m_size=1400;
  m_sendEvent = EventId ();
  m_filesize=0;
  m_rWnd=1000;
  m_chunksize=1400; //right now this value is hardcoded, change later.
  m_initialCWnd=2;
  m_cnCount=3;
  m_count=0;
  m_cnTimeout=Seconds(0.2);
  m_rto=Seconds(0.6);
}

CdnClient::~CdnClient ()
{
  NS_LOG_FUNCTION (this);
}

void
CdnClient::SetRemote (Ipv4Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress = Address(ip);
  m_peerPort = port;
}

void
CdnClient::SetRemote (Ipv6Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress = Address(ip);
  m_peerPort = port;
}

void
CdnClient::SetRemote (Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress = ip;
  m_peerPort = port;
}

void
CdnClient::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
CdnClient::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      if (Ipv4Address::IsMatchingType(m_peerAddress) == true)
        {
          m_socket->Bind ();
          m_socket->Connect (InetSocketAddress (Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
      else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
        {
          m_socket->Bind6 ();
          m_socket->Connect (Inet6SocketAddress (Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
    }
  m_socketList.push_back(m_socket);
  Ptr<CdnClientSubflow> subflow=CreateObject<CdnClientSubflow>();
  subflow->SetSocket(m_socket);
  subflow->SetClient(this);
  subflow->SetMain();
  m_subflowList.push_back(subflow);
  subflow->GoToStartApplication();
 
  //Not sending anything yet!
  // m_sendEvent = Simulator::Schedule (Seconds (0.0), &CdnClient::Send, this);
}

void
CdnClient::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
  Simulator::Cancel (m_sendEvent);
}




void
CdnClient::EstimateRTT(const SeqTsHeader& AckHdr)
{
  
  Time nextRtt =  m_rtt->AckSeq (SequenceNumber32(AckHdr.GetSeq ()) );
  if(nextRtt != 0)
  {
    m_lastRtt = nextRtt;
  }
}

//returns a viable subflow that has the minimum rtt.
  Ptr<CdnClientSubflow> CdnClient::GetNextSubflow(uint32_t *w)
  {
    std::vector<Ptr<CdnClientSubflow> >::iterator it;
    int64_t min_time_to_peer = 4294967295;
    Ptr<CdnClientSubflow> bestsubflow;
    uint32_t tw;
    uint32_t minw;
    for(it=m_subflowList.begin(); it!=m_subflowList.end(); it++)
      {
        if(!(*it)->IsAvailable(&tw))
          continue;
        if((*it)->GetRTT()<min_time_to_peer)
          {
            min_time_to_peer=(*it)->GetRTT();
            bestsubflow=(*it);
            minw=tw;
          }
        
      }
    *w=minw;
    return bestsubflow;
  }
  /* This function does: requests the next packet.
   * This is sending requests one at a time, rather than making a contigues request.
   */
  void CdnClient::SendWhatPossible()
  {
     if(m_nextTxSequence == 0)
      {
        m_nextTxSequence += 1;   
      }
    while (m_txBuffer.SizeFromSequence (m_nextTxSequence))
    {
      uint32_t w;
      Ptr <CdnClientSubflow> subflow=GetNextSubflow(&w);
      uint32_t packetsize=1;
      uint32_t nPacketsSent=0;

      uint32_t s = std::min(w, packetsize);  // Send no more than window
     
      SendDataPacket (subflow,m_nextTxSequence, s);
      nPacketsSent++;                             // Count sent this loop
      m_nextTxSequence += 1;                     // Advance next tx sequence
      
      
      }
  }

uint32_t CdnClient::SendDataPacket(Ptr<CdnClientSubflow> subflow, uint32_t seq, uint32_t maxSize)
  {
   
    uint32_t num=m_txBuffer.ReturnMaxPossible(maxSize, seq);

    Ptr<Packet> p;
  //create a syn header and add it to the packet
    CdnHeader cdnhdr;
    if (m_retxEvent.IsExpired () )
    { // Schedule retransmit
      m_rto = m_rtt->RetransmitTimeout ();
      NS_LOG_LOGIC (this << " SendDataPacket Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds () );
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClient
::ReTxTimeout, this);
    }
    for(uint32_t i=0; i<num; ++i)
      {    
        p = Create<Packet>(0);
        cdnhdr.SetSynType(4);
        cdnhdr.SetReqNumber(seq);
        p->AddHeader (cdnhdr);
        m_highTxMark= std::max (SequenceNumber32(seq + 1), m_highTxMark.Get ());    
        subflow->AddDataPacket (p);   
      }
  
    return 0;
  }
 
  uint32_t CdnClient::UnAckDataCount (void)
  {
   return m_nextTxSequence.Get () - m_txBuffer.HeadSequence ();  
  }
  void CdnClient::ConsumeData(void)
  {
  }
void CdnClient::ProcessAck(Ptr<Packet> p, CdnHeader Ack)
  {
    /*First have to check if the ack is in sequence!
     * will have to change this later, to reflect multiple subflows.*/
    if (Ack.GetReqNumber () < m_txBuffer.HeadSequence ())
    { // Case 1: Old ACK, ignored.
      NS_LOG_LOGIC ("Ignored ack of " << Ack.GetReqNumber());
    }
    else if (Ack.GetReqNumber () == m_txBuffer.HeadSequence ())
    { // Case 2: Potentially a duplicated ACK
     
      if (Ack.GetReqNumber () < m_nextTxSequence)
        {
          NS_LOG_LOGIC ("Dupack of " << Ack.GetReqNumber ());
          DupAck (Ack, ++m_dupAckCount);
        }
      // otherwise, the ACK is precisely equal to the nextTxSequence
      NS_ASSERT (Ack.GetReqNumber () <= m_nextTxSequence);
    }
    else if (Ack.GetReqNumber () > m_txBuffer.HeadSequence ())
    { // Case 3: New ACK, reset m_dupAckCount and update m_txBuffer
      
      NS_LOG_LOGIC ("New ack of " << Ack.GetReqNumber ());
      /*The packet has new information!*/
      if(p->GetSize()>0)
        {
          SeqTsHeader tempack;
          tempack.SetSeq(Ack.GetReqNumber());
      if (!m_rxBuffer.Add (p, tempack))
       { // Insert failed: No data or RX buffer full
         
         NS_ASSERT(0);
         return;
       }
        }
      uint32_t expectedSeq = m_rxBuffer.NextRxSequence ();
      if (expectedSeq < m_rxBuffer.NextRxSequence ())
        {
          ConsumeData();
        }
      NewAck (SequenceNumber32(Ack.GetReqNumber ()));
      m_dupAckCount = 0;
      SetSubflowrWnds();
    }
  }

void  CdnClient::SetSubflowrWnds()
{
  m_rWnd--;
   std::vector<Ptr<CdnClientSubflow> >::iterator it;
  
    for(it=m_subflowList.begin(); it!=m_subflowList.end(); it++)
      {
        (*it)->SetRwnd(m_rWnd);
        
      }
}
//Following the newreno policy! 
/* Cut cwnd and enter fast recovery mode upon triple dupack
 * Since all our chunks are at the same size we can modify the current
 * protocol implementation.*/
void CdnClient::DupAck (const CdnHeader& t, uint32_t count)
{
   NS_LOG_FUNCTION (this << count);
  if (count == m_retxThresh)
    { // retransmit packet on another subflow if necessary. 
      DoRetransmit ();
    }
  else if (m_limitedTx && m_txBuffer.SizeFromSequence (m_nextTxSequence) > 0)
    { // RFC3042 Limited transmit: Send a new packet for each duplicated ACK before fast retransmit
      NS_LOG_INFO ("Limited transmit");
      uint32_t w;
      Ptr<CdnClientSubflow> subflow = GetNextSubflow(&w);
      SendDataPacket (subflow,m_nextTxSequence, 1);
      m_nextTxSequence += 1;                    // Advance next tx sequence
    }
  else
    { 
      SendWhatPossible();
    }
 
}
void CdnClient::NewAck (const SequenceNumber32& seq)
{
        uint32_t ack=seq.GetValue();
        if(ack==(m_filesize+1))
        {
          std::cout<<"file completely served!\n";
          Simulator::Stop();
        }
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
      // On recieving a "New" ack we restart retransmission timer .. RFC 2988
      
      NS_LOG_LOGIC (this << " Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClient::ReTxTimeout, this);
  // Complete newAck processing
  DoNewAck (seq);
}
void CdnClient::DoNewAck (const SequenceNumber32& seq)
{
  /* Note that since the receive window is local, 
   * we don't need to send probes in 
   * our implementation.*/
  uint32_t ack=seq.GetValue();
  m_txBuffer.DiscardNumUpTo (ack);
  if (m_txBuffer.Available ()> 0)
    {
      uint32_t i;
      /*We add to the buffer the number of bytes that need to be requested.*/
      uint32_t m_lastDataAdded=std::min(m_remfromfile,m_txBuffer.Available());
      for (i=0; i<m_lastDataAdded; i++)
      {
         //I have setup a tx buffer with the number of the packets that I need!
         m_txBuffer.Add (i);
         m_remfromfile--;
      }
    
    }
  if (ack > m_nextTxSequence)
    {
      m_nextTxSequence = ack; // If advanced
    }
  /* if (m_txBuffer.Size () == 0)
    { // No retransmit timer if no data to retransmit
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
      }*/
  // Try to send more data
  
SendWhatPossible();
  
}
uint32_t CdnClient::ChunksInFlight ()
{
  NS_LOG_FUNCTION (this);
  return m_highTxMark.Get () - SequenceNumber32(m_txBuffer.HeadSequence ());
}
void  CdnClient::InitializeCwnd (void)
{
  /*
   * Initialize congestion window, default to 1 MSS (RFC2001, sec.1) and must
   * not be larger than 2 MSS (RFC2581, sec.3.1). Both m_initiaCWnd and
   * m_segmentSize are set by the attribute system in ns3::TcpSocket.
   */
  m_cWnd = m_initialCWnd;
}
void CdnClient::SetInitialCwnd (uint32_t cwnd)
{
  m_initialCWnd = cwnd;
}

void CdnClient::DoRetransmit ()
{
  uint32_t w;
  Ptr<CdnClientSubflow> subflow=GetNextSubflow(&w);
  SendDataPacket (subflow, m_txBuffer.HeadSequence (), 1);
  // In case of RTO, advance m_nextTxSequence
  m_nextTxSequence = std::max (m_nextTxSequence.Get (), m_txBuffer.HeadSequence () + 1);
  
}
void
CdnClient::Retransmit (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  // If all data are received (non-closing socket and nothing to send), just return
  if (SequenceNumber32(m_txBuffer.HeadSequence () )>= m_highTxMark) return;
  m_nextTxSequence = m_txBuffer.HeadSequence (); // Restart from highest Ack
  DoRetransmit ();                          // Retransmit the packet
}
void CdnClient::ReTxTimeout (void)
{
  /* NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
    {
      return;
    }
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer.HeadSequence () >= m_highTxMark)
    {
      return;
    }
  */
    Retransmit ();
}
 bool CdnClient::OutOfRange (uint32_t head, uint32_t tail) const
{

  // In all other cases, check if the sequence number is in range
  return (tail < m_rxBuffer.NextRxSequence () || m_rxBuffer.MaxRxSequence () <= head);
}
void CdnClient::PopulateBuffer (CdnHeader cdnhdr)
{
  m_highTxMark=SequenceNumber32(++m_nextTxSequence);
  m_txBuffer.SetHeadSequence (m_nextTxSequence);
  m_filesize=cdnhdr.GetFileSize();
  m_remfromfile=m_filesize;
  uint32_t i;
  /*We add to the buffer the number of bytes that need to be requested.*/
  uint32_t m_lastDataAdded=std::min(m_filesize,m_txBuffer.Available());
  for (i=1; i<=m_lastDataAdded; i++)
  {
                        //I have setup a tx buffer with the number of the packets that I need!
      m_txBuffer.Add (i);
      m_remfromfile--;
  }
  if(m_rxBuffer.NextRxSequence ()>m_filesize)
      {
        std::cout<<"Finished file transmit!\n";
      }
    else
      {
            if(m_txBuffer.Size () == 0)
              {
                std::cout<<"finished file transmit\n";
              }
            else
              {
                SendWhatPossible();
              }
            
      }
}


} // Namespace ns3



/* What is missing so far from the code:
   First step to replace it such that you can do rtt estimation.

   F- Make contiguous requests for packets????
   J- Make sure you actually process the received data.
   M- I have to find the correct place to decrement my receive window and I have to figure out how to manage the initial values.
   %%add the optimizations performed by mptcp.
   %%not keeping track of which subflow each packet was sent on => retransmission could be suboptimal, fix that later!
*/

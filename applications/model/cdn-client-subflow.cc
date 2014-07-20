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

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CdnClientSubflow")
  ;
NS_OBJECT_ENSURE_REGISTERED (CdnClientSubflow)
  ;

TypeId
CdnClientSubflow::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CdnClientSubflow")
    .SetParent<Application> ()
    .AddConstructor<CdnClientSubflow> ()
    .AddAttribute ("MaxPackets",
                   "The maximum number of packets the application will send",
                   UintegerValue (100),
                   MakeUintegerAccessor (&CdnClientSubflow::m_count),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RttEstimatorType",
                   "Type of RttEstimator objects.",
                   TypeIdValue (RttMeanDeviation::GetTypeId ()),
                   MakeTypeIdAccessor (&CdnClientSubflow::m_rttTypeId),
                   MakeTypeIdChecker ())
    .AddAttribute ("Interval",
                   "The time to wait between packets", TimeValue (Seconds (1.0)),
                   MakeTimeAccessor (&CdnClientSubflow::m_interval),
                   MakeTimeChecker ())
    .AddAttribute ("RemoteAddress",
                   "The destination Address of the outbound packets",
                   AddressValue (),
                   MakeAddressAccessor (&CdnClientSubflow::m_peerAddress),
                   MakeAddressChecker ())
    .AddAttribute ("RemotePort", "The destination port of the outbound packets",
                   UintegerValue (100),
                   MakeUintegerAccessor (&CdnClientSubflow::m_peerPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PacketSize",
                   "Size of packets generated. The minimum packet size is 12 bytes which is the size of the header carrying the sequence number and the time stamp.",
                   UintegerValue (1024),
                   MakeUintegerAccessor (&CdnClientSubflow::m_size),
                   MakeUintegerChecker<uint32_t> (12,1500))
    .AddAttribute ("ReTxThreshold", "Threshold for fast retransmit",
                    UintegerValue (3),
                    MakeUintegerAccessor (&CdnClientSubflow::m_retxThresh),
                    MakeUintegerChecker<uint32_t> ())
       .AddAttribute ("LimitedTransmit", "Enable limited transmit",
		    BooleanValue (false),
		    MakeBooleanAccessor (&CdnClientSubflow::m_limitedTx),
		    MakeBooleanChecker ())
       .AddTraceSource ("CongestionWindow",
                     "The TCP connection's congestion window",
                     MakeTraceSourceAccessor (&CdnClientSubflow::m_cWnd))
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
  m_sendEvent = EventId ();
  m_filesize=0;
  m_rWnd=1000;
  m_chunksize=1400; //right now this value is hardcoded, change later.
  m_initialCWnd=2;
  m_cnCount=3;
  m_count=0;
  m_cnTimeout=Seconds(0.2);
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
          Connect();
        }
      else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
        {
          m_socket->Bind6 ();
          m_socket->Connect (Inet6SocketAddress (Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
    }
  ObjectFactory rttFactory;
  rttFactory.SetTypeId (m_rttTypeId);
  m_rtt = rttFactory.Create<ns3::RttEstimator> ();
  m_rtt->Reset ();
  InitializeCwnd();
  m_socket->SetRecvCallback (MakeCallback (&CdnClient::HandleRead, this));
  //Not sending anything yet!
  // m_sendEvent = Simulator::Schedule (Seconds (0.0), &CdnClient::Send, this);
}

void
CdnClient::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
  Simulator::Cancel (m_sendEvent);
}


  /*Do not use if you want to piggyback acknowlegements */
void
CdnClient::Send (uint32_t syntype)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_sendEvent.IsExpired ());
  Ptr<Packet> p;
  if (syntype==2)
    {
      p = Create<Packet> (m_size-(8+4+4)); // 8+4 : the size of the seqTs header and 4 for the size of the size of the cdn header 
    }
  else
    {
      /*The packet is part of the three way handshake => no payload.*/
       p = Create<Packet>(0);
    }
  /*Putting in the acknowledgement, we have nothing to acknowledge here so
   * we put a -1 instead to show that this packet is not acknowledging anything.*/
  SeqTsHeader Ack;
  Ack.SetSeq(-1);
  p->AddHeader (Ack);

  /*Packet header containing the sequence number and time stamp.*/
  SeqTsHeader seqTs;
  seqTs.SetSeq (m_nextTxSequence); 
  p->AddHeader (seqTs);

  //create a syn header and add it to the packet
  CdnHeader cdnhdr;
  cdnhdr.SetSynType(syntype);
  p->AddHeader (cdnhdr);
  

  std::stringstream peerAddressStringStream;
  if (Ipv4Address::IsMatchingType (m_peerAddress))
    {
      peerAddressStringStream << Ipv4Address::ConvertFrom (m_peerAddress);
    }
  else if (Ipv6Address::IsMatchingType (m_peerAddress))
    {
      peerAddressStringStream << Ipv6Address::ConvertFrom (m_peerAddress);
    }

  //m_rto = m_rtt->RetransmitTimeout ();
  //m_rtt->SentSeq (SequenceNumber32(m_nextTxSequence), 1);
  m_rto = m_rtt->RetransmitTimeout ();
  if (syntype==0 || syntype==3)
    {
      if (m_cnCount == 0) // double check
        { // No more connection retries, give up
          NS_LOG_LOGIC ("Connection failed.");
          std::cout<<"connection failed!\n";
          return;
        }
      else
        { // Exponential backoff of connection time out
          int backoffCount = 0x1 << (3 - m_cnCount);
          m_rto = m_cnTimeout * backoffCount;
          m_cnCount--;
        }
    }

  if ((m_socket->Send (p)) >= 0)
    {
     
      NS_LOG_INFO ("TraceDelay TX " << m_size << " bytes to "
                                    << peerAddressStringStream.str () << " Uid: "
                                    << p->GetUid () << " Time: "
                                    << (Simulator::Now ()).GetSeconds ());

    }
  else
    {
      NS_LOG_INFO ("Error while sending " << m_size << " bytes to "
                                          << peerAddressStringStream.str ());
    }
    if (m_retxEvent.IsExpired () && (syntype==0))
    { // Retransmit SYN / SYN+ACK / FIN / FIN+ACK to guard against lost
      NS_LOG_LOGIC ("Schedule retransmission timeout at time "
                    << Simulator::Now ().GetSeconds () << " to expire at time "
                    << (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClient::Send, this, 0);
    }
  /*if (m_nextTxSequence < m_count)
    {
       m_sendEvent = Simulator::Schedule (m_interval, &CdnClient::Send, this);
      }*/
}
  void CdnClient::Connect(void)
  {
    //first send a syn to the other end and set your current state to Syn-Sent =0.
    m_state=0;
    m_sendEvent=Simulator::Schedule(Seconds (0.0), &CdnClient::Send, this, 0);
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

  /*Reads the response from the CdnServer.*/
  void
CdnClient::HandleRead (Ptr<Socket> socket)
  {
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom (from)))
      {
        if (packet->GetSize () > 0)
        {
          CdnHeader cdnhdr;
          packet->RemoveHeader (cdnhdr);
          SeqTsHeader seqTs;
          packet->RemoveHeader (seqTs);
          SeqTsHeader AckHdr;
          packet->RemoveHeader(AckHdr);
          CdnHeader ToSendCdnHdr;


          switch(cdnhdr.GetSynType())
            {
            case 1:
              {
                /* It was a syn-ack, the server responds and includes
                 * the size of the file to be downloaded
                 * In its response.*/
                if(m_state==0)
                  {
                    m_state=2;
                    m_highTxMark=SequenceNumber32(++m_nextTxSequence);
                    m_retxEvent.Cancel ();
                    m_txBuffer.SetHeadSequence (m_nextTxSequence);
                    /*this packet does not contain an ACK so we don't need to process it.*/
                    //ProcessAck(AckHdr);
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
                  }
                break;
              }
            case 2:
              {
                /*In case of a Pure Ack packet. NEEDS MODIFICATION!!*/
                ProcessAck(packet, AckHdr);
                return;
              }
            case 4:
              {
                
                if(AckHdr.GetSeq()!=(-1))
                  {
             
                  
                    EstimateRTT(AckHdr);      
                     if (packet->GetSize ()
                      && OutOfRange (AckHdr.GetSeq (), AckHdr.GetSeq () + 1))
                     {
                        return;
                     }
                    ProcessAck(packet,AckHdr);
                    
                    m_rWnd--;
                  }
                return;

              }
            }

            /*This part of the code is sending the ack
             * for the packet that was just recieved.*/
          
            /*create a packet of length zero.*/
            Ptr<Packet> ToSendPacket=Create<Packet> (0);
 
            //inspired by TcpSocketBase, receiving data.
            uint32_t expectedSeq = m_rxBuffer.NextRxSequence ();

            SeqTsHeader Ack;
            if (!m_rxBuffer.Add (packet, seqTs))
            { // Insert failed: No data or RX buffer full
              //(make sure you know what needs to be done next)
              Ack.SetSeq(m_rxBuffer.NextRxSequence ());
              return;
            }
 
            /*our application currently supports only data of the same size!
             *what we have here is only sending an ack it is not piggybacking requests! */
           Ack.SetSeq(m_rxBuffer.NextRxSequence ());
           if (expectedSeq < m_rxBuffer.NextRxSequence ())
           {
              ConsumeData();
           }
           Ack.SetTs(seqTs.GetTsInt());
           ToSendPacket->AddHeader (Ack);
           SeqTsHeader packetHdr;
           packetHdr.SetSeq(-1);
           ToSendCdnHdr.SetSynType(2);  
           ToSendPacket->AddHeader (packetHdr);
           ToSendPacket->AddHeader(ToSendCdnHdr);
           //m_rto = m_rtt->RetransmitTimeout ();
           m_socket->SendTo (ToSendPacket, 0, from); 
        }
        
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
      uint32_t w = AvailableWindow (); // Get available window size
      uint32_t packetsize=1;
      uint32_t nPacketsSent=0;

      // Stop sending if we need to wait for a larger Tx window (prevent silly window syndrome)
      if (w<1 && m_txBuffer.SizeFromSequence (m_nextTxSequence) > w)
        {
          break; // No more
        }
      uint32_t s = std::min(w, packetsize);  // Send no more than window
      /*we are */
      SendDataPacket (m_nextTxSequence, s);
      nPacketsSent++;                             // Count sent this loop
      m_nextTxSequence += 1;                     // Advance next tx sequence
      
      
  }
  }
/* I think this is also wrong. */
  uint32_t CdnClient::SendDataPacket(uint32_t seq, uint32_t maxSize)
  {
   
    uint32_t num=m_txBuffer.ReturnMaxPossible(maxSize, seq);

    Ptr<Packet> p;
    
    SeqTsHeader Ack;
    Ack.SetSeq(-1);
   
    /*Packet header containing the sequence number and time stamp.*/
    SeqTsHeader seqTs; 
  //create a syn header and add it to the packet
    CdnHeader cdnhdr;
    if (m_retxEvent.IsExpired () )
    { // Schedule retransmit
      m_rto = m_rtt->RetransmitTimeout ();
      NS_LOG_LOGIC (this << " SendDataPacket Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds () );
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClient::ReTxTimeout, this);
    }
    for(uint32_t i=0; i<num; ++i)
      {    
        p = Create<Packet>(0);
        p->AddHeader (Ack);
        seqTs.SetSeq (seq); 
        p->AddHeader (seqTs);
        cdnhdr.SetSynType(4);
        cdnhdr.SetReqNumber(seq);
        p->AddHeader (cdnhdr);
        //m_rto = m_rtt->RetransmitTimeout ();
        m_rtt->SentSeq (SequenceNumber32(seq), 1);
        m_highTxMark= std::max (SequenceNumber32(seq + 1), m_highTxMark.Get ());
        m_rto = m_rtt->RetransmitTimeout ();
        if ((m_socket->Send (p)) >= 0)
        {
          seq++;
        }
      }
  
    return 0;
  }
  uint32_t CdnClient::AvailableWindow(void)
  {
  uint32_t unack = UnAckDataCount (); // Number of outstanding bytes
  uint32_t win = Window (); // Number of bytes allowed to be outstanding
  NS_LOG_LOGIC ("UnAckCount=" << unack << ", Win=" << win);
  return (win < unack) ? 0 : (win - unack);
  }
  uint32_t CdnClient::Window (void)
  {
   return std::min (m_rWnd.Get (), m_cWnd.Get ());;
  }
  uint32_t
 CdnClient::UnAckDataCount (void)
  {
   return m_nextTxSequence.Get () - m_txBuffer.HeadSequence ();  
  }
  void CdnClient::ConsumeData(void)
  {
  }
void CdnClient::ProcessAck(Ptr<Packet> p, SeqTsHeader Ack)
  {
    /*First have to check if the ack is in sequence!
     * will have to change this later, to reflect multiple subflows.*/
    if (Ack.GetSeq () < m_txBuffer.HeadSequence ())
    { // Case 1: Old ACK, ignored.
      NS_LOG_LOGIC ("Ignored ack of " << Ack.GetSeq());
    }
    else if (Ack.GetSeq () == m_txBuffer.HeadSequence ())
    { // Case 2: Potentially a duplicated ACK
     
      if (Ack.GetSeq () < m_nextTxSequence)
        {
          NS_LOG_LOGIC ("Dupack of " << Ack.GetSeq ());
          DupAck (Ack, ++m_dupAckCount);
        }
      // otherwise, the ACK is precisely equal to the nextTxSequence
      NS_ASSERT (Ack.GetSeq () <= m_nextTxSequence);
    }
    else if (Ack.GetSeq () > m_txBuffer.HeadSequence ())
    { // Case 3: New ACK, reset m_dupAckCount and update m_txBuffer
      
      NS_LOG_LOGIC ("New ack of " << Ack.GetSeq ());
      /*The packet has new information!*/
      if(p->GetSize()>0)
        {
      if (!m_rxBuffer.Add (p, Ack))
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
      NewAck (SequenceNumber32(Ack.GetSeq ()));
      m_dupAckCount = 0;
    }
  }
//Following the newreno policy! 
/* Cut cwnd and enter fast recovery mode upon triple dupack
 * Since all our chunks are at the same size we can modify the current
 * protocol implementation.*/
void CdnClient::DupAck (const SeqTsHeader& t, uint32_t count)
{
   NS_LOG_FUNCTION (this << count);
  if (count == m_retxThresh && !m_inFastRec)
    { // triple duplicate ack triggers fast retransmit (RFC2582 sec.3 bullet #1)
      
      m_ssThresh = std::max ((uint32_t)2, ChunksInFlight () / 2);
      m_cWnd = m_ssThresh + 3 ;
      m_recover = m_highTxMark;
      m_inFastRec = true;
      DoRetransmit ();
      }
  else if (m_inFastRec)
    { // Increase cwnd for every additional dupack (RFC2582, sec.3 bullet #3)
      m_cWnd += 1;
      NS_LOG_INFO ("Dupack in fast recovery mode. Increase cwnd to " << m_cWnd);
      SendWhatPossible();
    }
  else if (!m_inFastRec && m_limitedTx && m_txBuffer.SizeFromSequence (m_nextTxSequence) > 0)
    { // RFC3042 Limited transmit: Send a new packet for each duplicated ACK before fast retransmit
      NS_LOG_INFO ("Limited transmit");
      
      SendDataPacket (m_nextTxSequence, 1);
      m_nextTxSequence += 1;                    // Advance next tx sequence
    };
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
      m_rto = m_rtt->RetransmitTimeout ();
      NS_LOG_LOGIC (this << " Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClient::ReTxTimeout, this);
   if (m_inFastRec && seq < m_recover)
    { // Partial ACK, partial window deflation (RFC2582 sec.3 bullet #5 paragraph 3)
      m_cWnd -= seq - SequenceNumber32(m_txBuffer.HeadSequence ());
      m_cWnd += 1;  // increase cwnd
      NS_LOG_INFO ("Partial ACK in fast recovery: cwnd set to " << m_cWnd);
      DoNewAck (seq); // update m_nextTxSequence and send new data if allowed by window
      DoRetransmit (); // Assume the next seq is lost. Retransmit lost packet
      return;
    }
   else if (m_inFastRec && seq >= m_recover)
    { // Full ACK (RFC2582 sec.3 bullet #5 paragraph 2, option 1)
      m_cWnd = std::min (m_ssThresh, ChunksInFlight () + 1);
      m_inFastRec = false;
      NS_LOG_INFO ("Received full ACK. Leaving fast recovery with cwnd set to " << m_cWnd);
    }
    // Increase of cwnd based on current phase (slow start or congestion avoidance)
  if (m_cWnd < m_ssThresh)
    { // Slow start mode, add one segSize to cWnd. Default m_ssThresh is 65535. (RFC2001, sec.1)
      m_cWnd += 1;
      NS_LOG_INFO ("In SlowStart, updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
    }
  else
    { // Congestion avoidance mode, increase by (segSize*segSize)/cwnd. (RFC2581, sec.3.1)
      // To increase cwnd for one segSize per RTT, it should be (ackBytes*segSize)/cwnd
      double adder = static_cast<double> (1) / m_cWnd.Get ();
      adder = std::max (1.0, adder);
      m_cWnd += static_cast<uint32_t> (adder);
      NS_LOG_INFO ("In CongAvoid, updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
    }

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
  if (m_state == 0)
    {
      if (m_cnCount > 0)
        {
          Send (0);
          m_cnCount--;
        }
      else
        {
          std::cout<<"connection failed, number of retries exceeded!\n";
        }
      return;
    }
 

  SendDataPacket (m_txBuffer.HeadSequence (), 1);
  // In case of RTO, advance m_nextTxSequence
  m_nextTxSequence = std::max (m_nextTxSequence.Get (), m_txBuffer.HeadSequence () + 1);
  
}
void
CdnClient::Retransmit (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  m_inFastRec = false;
  // If all data are received (non-closing socket and nothing to send), just return
  if (SequenceNumber32(m_txBuffer.HeadSequence () )
>= m_highTxMark) return;

  // According to RFC2581 sec.3.1, upon RTO, ssthresh is set to half of flight
  // size and cwnd is set to 1*MSS, then the lost packet is retransmitted and
  // TCP back to slow start
  m_ssThresh = std::max ( (uint32_t

)2, ChunksInFlight () / 2);
  m_cWnd = 1;
  m_nextTxSequence = m_txBuffer.HeadSequence (); // Restart from highest Ack
  NS_LOG_INFO ("RTO. Reset cwnd to " << m_cWnd <<
               ", ssthresh to " << m_ssThresh << ", restart from seqnum " << m_nextTxSequence);
  m_rtt->IncreaseMultiplier ();             // Double the next RTO
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

    Retransmit ();*/
}
 bool CdnClient::OutOfRange (uint32_t head, uint32_t tail) const
{

  // In all other cases, check if the sequence number is in range
  return (tail < m_rxBuffer.NextRxSequence () || m_rxBuffer.MaxRxSequence () <= head);
}

} // Namespace ns3



/* What is missing so far from the code:
   First step to replace it such that you can do rtt estimation.

   F- Make contiguous requests for packets????
   J- Make sure you actually process the received data.
   M- I have to find the correct place to decrement my receive window and I have to figure out how to manage the initial values.
*/
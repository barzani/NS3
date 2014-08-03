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
    .AddAttribute ("RttEstimatorType",
                   "Type of RttEstimator objects.",
                   TypeIdValue (RttMeanDeviation::GetTypeId ()),
                   MakeTypeIdAccessor (&CdnClientSubflow::m_rttTypeId),
                   MakeTypeIdChecker ())
    .AddAttribute ("Interval",
                   "The time to wait between packets", TimeValue (Seconds (1.0)),
                   MakeTimeAccessor (&CdnClientSubflow::m_interval),
                   MakeTimeChecker ())
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

  CdnClientSubflow::CdnClientSubflow()
  :m_rtt(0),
   m_inFastRec (false)
{
  NS_LOG_FUNCTION (this);
  
  m_nextTxSequence = 0;
  m_state=-1;
  m_socket = 0;
  m_sendEvent = EventId ();
  m_filesize=0;
  m_rWnd=2000;
  m_chunksize=1400; //right now this value is hardcoded, change later.
  m_initialCWnd=2;
  m_cnCount=3;
  m_count=0;
  m_cnTimeout=Seconds(0.2);
  m_parent=0;
  m_ismain=false;
}

CdnClientSubflow::~CdnClientSubflow ()
{
  NS_LOG_FUNCTION (this);
}


  void CdnClientSubflow::SetMain(void)
  {
     NS_LOG_FUNCTION (this);
    m_ismain=true;
  }

void
CdnClientSubflow::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void CdnClientSubflow::SetSocket(Ptr<Socket> socket)
{
   NS_LOG_FUNCTION (this);
  m_socket=socket;
}

  void CdnClientSubflow::SetClient(Ptr<CdnClient> client)
{
   NS_LOG_FUNCTION (this);
  m_parent=client;
}
void CdnClientSubflow::StartApplication ()
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT(m_socket!=0);

  
  ObjectFactory rttFactory;
  rttFactory.SetTypeId (m_rttTypeId);
  m_rtt = rttFactory.Create<ns3::RttEstimator> ();
  m_rtt->Reset ();
  InitializeCwnd();
  m_socket->SetRecvCallback (MakeCallback (&CdnClientSubflow::HandleRead, this));
  Connect();
  //Not sending anything yet!
  // m_sendEvent = Simulator::Schedule (Seconds (0.0), &CdnClient::Send, this);
}

  void CdnClientSubflow::GoToStartApplication(void)
  {
     NS_LOG_FUNCTION (this);
    StartApplication();
  }
void
CdnClientSubflow::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
  Simulator::Cancel (m_sendEvent);
}


  /*Do not use if you want to piggyback acknowlegements */
void
CdnClientSubflow::Send (uint32_t syntype)
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

  //create a syn header and add it to the packet
  CdnHeader cdnhdr;
  cdnhdr.SetSynType(syntype);
  p->AddHeader (cdnhdr);

  /*Putting in the acknowledgement, we have nothing to acknowledge here so
   * we put a -1 instead to show that this packet is not acknowledging anything.*/
  SeqTsHeader Ack;
  Ack.SetSeq(-1);
  p->AddHeader (Ack);

  /*Packet header containing the sequence number and time stamp.*/
  SeqTsHeader seqTs;
  seqTs.SetSeq (m_nextTxSequence); 
  p->AddHeader (seqTs);

  
  


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
    
   m_socket->Send (p);
 
    if (m_retxEvent.IsExpired () && (syntype==0 || syntype==3))
    { // Retransmit SYN  to guard against lost
      NS_LOG_LOGIC ("Schedule retransmission timeout at time "
                    << Simulator::Now ().GetSeconds () << " to expire at time "
                    << (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClientSubflow::Send, this, 0);
    }
  /*if (m_nextTxSequence < m_count)
    {
       m_sendEvent = Simulator::Schedule (m_interval, &CdnClient::Send, this);
      }*/
}
  void CdnClientSubflow::Connect(void)
  {
     NS_LOG_FUNCTION (this);
    //first send a syn to the other end and set your current state to Syn-Sent =0.
    m_state=0;
    if(m_ismain)
      {
        m_sendEvent=Simulator::Schedule(Seconds (0.0), &CdnClientSubflow::Send, this, 0);
      }
    else
      {
      
	m_sendEvent=Simulator::Schedule(Seconds (0.0), &CdnClientSubflow::Send, this, 3);
      }
  }

void CdnClientSubflow::HalfWindow(void)
{
  m_cWnd=(m_cWnd/2.0);
  if(m_cWnd<2)
    {
      m_cWnd=2;
    }
  m_ssThresh=(m_ssThresh/2.0);
  if(m_ssThresh<2)
    {
      m_ssThresh=2;
    }
}
 int64_t CdnClientSubflow::GetRTT(void)
  {
    
    return m_rtt->GetCurrentEstimate().ToInteger (Time::MS);
  }


uint32_t CdnClientSubflow::Getcwnd(void)
{
  return m_cWnd;
}


  bool CdnClientSubflow::IsAvailable(uint32_t * w)
  {
    if(m_state==0)
      {
        *w=0;
        return false;
      }

    *w= AvailableWindow (); // Get available window size
    if (*w<1 && m_txBuffer.SizeFromSequence (m_nextTxSequence) > *w)
      {
        *w=0;
      }
    if(*w>0)
      {
	return true;
      }
    return false;
  }

void
CdnClientSubflow::EstimateRTT(const SeqTsHeader& AckHdr)
{
   NS_LOG_FUNCTION (this);
  Time nextRtt =  m_rtt->AckSeq (SequenceNumber32(AckHdr.GetSeq ()) );
  if(nextRtt != 0)
  {
    m_lastRtt = nextRtt;
  }
}

  /*Reads the response from the CdnServer.*/
  void
CdnClientSubflow::HandleRead (Ptr<Socket> socket)
  {
     NS_LOG_FUNCTION (this);
    Ptr<Packet> packet;
    Address from;
    
    while ((packet = socket->RecvFrom (from)))
      {
        if (packet->GetSize () > 0)
        {
          
          SeqTsHeader seqTs;
          packet->RemoveHeader (seqTs);
          SeqTsHeader AckHdr;
          packet->RemoveHeader(AckHdr);
          CdnHeader ToSendCdnHdr;
          CdnHeader cdnhdr;
          packet->RemoveHeader (cdnhdr);

          switch(cdnhdr.GetSynType())
            {
            case 1:
              {
                /* It was a syn-ack, the server responds and includes
                 * the size of the file to be downloaded
                 * In its response.*/
                if(m_state==0)
                  {
		    m_filesize=cdnhdr.GetFileSize();
                    m_state=2;
                    m_highTxMark=SequenceNumber32(++m_nextTxSequence);
                    m_retxEvent.Cancel ();
                    m_txBuffer.SetHeadSequence (m_nextTxSequence);
                    /*this packet does not contain an ACK so we don't need to process it.*/
                    //ProcessAck(AckHdr);
                    if(m_ismain)
                      {
         	         m_parent->PopulateBuffer(cdnhdr);
                      }
                    else
                      {
                        m_parent->SendWhatPossible();


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
                    //remember to send up the packet to the upper layer!;
                    m_parent->ProcessAck(packet,cdnhdr);                 
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
	   ToSendCdnHdr.SetSynType(2);  
           ToSendPacket->AddHeader(ToSendCdnHdr);
           Ack.SetTs(seqTs.GetTsInt());
           ToSendPacket->AddHeader (Ack);
           SeqTsHeader packetHdr;
           packetHdr.SetSeq(-1);
           ToSendPacket->AddHeader (packetHdr);
          
           //m_rto = m_rtt->RetransmitTimeout ();
           m_socket->SendTo (ToSendPacket, 0, from); 
        }
        
      }
    if(m_rxBuffer.NextRxSequence ()>m_filesize)
      {
        std::cout<<"Finished file transmit! in subflow!\n";
        m_parent->SendWhatPossible();
      }
    else
      {
            if(m_txBuffer.Size () == 0)
              {
                std::cout<<"nothing to send"<<this<<"\n";
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
  void CdnClientSubflow::SendWhatPossible()
  {
     NS_LOG_FUNCTION (this);

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
      if(SendDataPacket (m_nextTxSequence, s))
        {
          nPacketsSent++;                             // Count sent this loop
          m_nextTxSequence += 1;
        }// Advance next tx sequence   
  }
}
  Ptr<Packet> CdnClientSubflow::GetChunk(uint16_t reqnum, Ptr<Packet> packet)
  {

    NS_LOG_FUNCTION (this);
   
    packet = m_txBuffer.CopyFromSequence (1, reqnum);

    return packet;
  }
void CdnClientSubflow::SetRwnd(uint32_t rWnd)
  {
     NS_LOG_FUNCTION (this);
    m_rWnd=rWnd;
  }
/* I think this is also wrong. */
  uint32_t CdnClientSubflow::SendDataPacket(uint32_t seq, uint32_t maxSize)
  {

    NS_LOG_FUNCTION (this);
      
    uint32_t num=m_txBuffer.ReturnMaxPossible(maxSize, seq);
   
    Ptr<Packet> p;
   
    SeqTsHeader Ack;
    Ack.SetSeq(-1);
   
    /*Packet header containing the sequence number and time stamp.*/
    SeqTsHeader seqTs; 
    uint32_t count;
    count=0;
    if (m_retxEvent.IsExpired () )
    { // Schedule retransmit
      m_rto = m_rtt->RetransmitTimeout ();
      NS_LOG_LOGIC (this << " SendDataPacket Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds () );
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClientSubflow::ReTxTimeout, this);
    }
    for(uint32_t i=0; i<num; ++i)
      {  
	//This is where I have a problem, i have to extract the packet from the buffer instead of sending an empty one!
        p = GetChunk(seq, p);
        //CdnHeader cdnhdr;
        //p->RemoveHeader(cdnhdr);
        // std::cout<<"this is the packet being sent from  " <<this <<"with seq number "<< cdnhdr.GetReqNumber() << "\n";
        //p->AddHeader(cdnhdr);
        p->AddHeader (Ack);
        seqTs.SetSeq (seq); 
        p->AddHeader (seqTs);
        
        //m_rto = m_rtt->RetransmitTimeout ();
        m_rtt->SentSeq (SequenceNumber32(seq), 1);
        m_highTxMark= std::max (SequenceNumber32(seq + 1), m_highTxMark.Get ());
        m_rto = m_rtt->RetransmitTimeout ();
        if ((m_socket->Send (p)) >= 0)
        {
          seq++;
          count++;
        }
      }

    return count;
  }
  uint32_t CdnClientSubflow::AvailableWindow(void)
  {
   
  uint32_t unack = UnAckDataCount (); // Number of outstanding bytes
  uint32_t win = Window (); // Number of bytes allowed to be outstanding
  NS_LOG_LOGIC ("UnAckCount=" << unack << ", Win=" << win);
  return (win < unack) ? 0 : (win - unack);
  }
  
  uint32_t CdnClientSubflow::Window (void)
  {
    //  std::cout<<"this is rwnd "<<this<<" "<<std::min (m_rWnd.Get (), m_cWnd.Get ())<<"rwnd is "<<m_rWnd.Get ()<< "cwnd is "<<m_cWnd.Get ()<<"\n";
   return std::min (m_rWnd.Get (), m_cWnd.Get ());
  }
  uint32_t
 CdnClientSubflow::UnAckDataCount (void)
  {

   return m_nextTxSequence.Get () - m_txBuffer.HeadSequence ();  
  }
  void CdnClientSubflow::ConsumeData(void)
  {
     NS_LOG_FUNCTION (this);
  }
void CdnClientSubflow::ProcessAck(Ptr<Packet> p, SeqTsHeader Ack)
  {
     NS_LOG_FUNCTION (this);
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
void CdnClientSubflow::DupAck (const SeqTsHeader& t, uint32_t count)
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
void CdnClientSubflow::NewAck (const SequenceNumber32& seq)
{
        NS_LOG_FUNCTION (this);
      

      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
      // On recieving a "New" ack we restart retransmission timer .. RFC 2988
      m_rto = m_rtt->RetransmitTimeout ();
      NS_LOG_LOGIC (this << " Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &CdnClientSubflow::ReTxTimeout, this);
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
void CdnClientSubflow::DoNewAck (const SequenceNumber32& seq)
{
   NS_LOG_FUNCTION (this);
  /* Note that since the receive window is local, 
   * we don't need to send probes in 
   * our implementation.*/
  uint32_t ack=seq.GetValue();
  m_txBuffer.DiscardNumUpTo (ack);
  m_txBuffer.DiscardUpTo(ack);

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
uint32_t CdnClientSubflow::ChunksInFlight ()
{
  NS_LOG_FUNCTION (this);
  return m_highTxMark.Get () - SequenceNumber32(m_txBuffer.HeadSequence ());
}
void  CdnClientSubflow::InitializeCwnd (void)
{
   NS_LOG_FUNCTION (this);
  /*
   * Initialize congestion window, default to 1 MSS (RFC2001, sec.1) and must
   * not be larger than 2 MSS (RFC2581, sec.3.1). Both m_initiaCWnd and
   * m_segmentSize are set by the attribute system in ns3::TcpSocket.
   */
  m_cWnd = m_initialCWnd;
}
void CdnClientSubflow::SetInitialCwnd (uint32_t cwnd)
{
   NS_LOG_FUNCTION (this);
  m_initialCWnd = cwnd;
}

void CdnClientSubflow::DoRetransmit ()
{

   NS_LOG_FUNCTION (this);
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
CdnClientSubflow::Retransmit (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  m_inFastRec = false;
  // If all data are received (non-closing socket and nothing to send), just return
  if (SequenceNumber32(m_txBuffer.HeadSequence () )>= m_highTxMark) return;

  // According to RFC2581 sec.3.1, upon RTO, ssthresh is set to half of flight
  // size and cwnd is set to 1*MSS, then the lost packet is retransmitted and
  // TCP back to slow start
  m_ssThresh = std::max ( (uint32_t)2, ChunksInFlight () / 2);
  m_cWnd = 1;
  m_nextTxSequence = m_txBuffer.HeadSequence (); // Restart from highest Ack
  NS_LOG_INFO ("RTO. Reset cwnd to " << m_cWnd <<
               ", ssthresh to " << m_ssThresh << ", restart from seqnum " << m_nextTxSequence);
  m_rtt->IncreaseMultiplier ();             // Double the next RTO
  DoRetransmit ();                          // Retransmit the packet
}
void CdnClientSubflow::ReTxTimeout (void)
{
   NS_LOG_FUNCTION (this);
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
      }*/

    Retransmit ();
}
 bool CdnClientSubflow::OutOfRange (uint32_t head, uint32_t tail) const
{
   NS_LOG_FUNCTION (this);
  // In all other cases, check if the sequence number is in range
  return (tail < m_rxBuffer.NextRxSequence () || m_rxBuffer.MaxRxSequence () <= head);
}

void CdnClientSubflow::AddDataPacket(Ptr<Packet> packet)
{
   NS_LOG_FUNCTION (this); 
   m_txBuffer.SetSize(packet->GetSize());
   m_txBuffer.Add(packet);   
   m_txBuffer.Add(1);

   SendWhatPossible();
}

} // Namespace ns3



/* What is missing so far from the code:
   First step to replace it such that you can do rtt estimation.

   F- Make contiguous requests for packets????
   J- Make sure you actually process the received data.
   M- I have to find the correct place to decrement my receive window and I have to figure out how to manage the initial values.
*/

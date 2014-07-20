/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 *  Copyright (c) 2007,2008,2009 INRIA, UDCAST
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
 * Author: Amine Ismail <amine.ismail@sophia.inria.fr>???????? ? what?where is THE FOR
 *                      <amine.ismail@udcast.com>
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
#include "packet-loss-counter.h"
#include "cdn-server.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CdnServer")
  ;
NS_OBJECT_ENSURE_REGISTERED (CdnServer)
  ;


TypeId
CdnServer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CdnServer")
    .SetParent<Application> ()
    .AddConstructor<CdnServer> ()
    .AddAttribute ("Port",
                   "Port on which we listen for incoming packets.",
                   UintegerValue (100),
                   MakeUintegerAccessor (&CdnServer::m_port),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PacketWindowSize",
                   "The size of the window used to compute the packet loss. This value should be a multiple of 8.",
                   UintegerValue (32),
                   MakeUintegerAccessor (&CdnServer::GetPacketWindowSize,
                                         &CdnServer::SetPacketWindowSize),
                   MakeUintegerChecker<uint16_t> (8,256))
    .AddAttribute ("PacketSize",
                   "The of a packet.",
                   UintegerValue (1400),
                   MakeUintegerAccessor (&CdnServer::m_chunksize),
                   MakeUintegerChecker<uint16_t> ())
  ;
  return tid;
}

CdnServer::CdnServer ()
  : m_lossCounter (0)
{
  NS_LOG_FUNCTION (this);
  m_received=0;
  m_state=0;
  m_sent=0;
  m_filesize=30;
  m_txBuffer.SetMaxBufferSize (m_filesize);
}

CdnServer::~CdnServer ()
{
  NS_LOG_FUNCTION (this);
}

uint16_t
CdnServer::GetPacketWindowSize () const
{
  NS_LOG_FUNCTION (this);
  return m_lossCounter.GetBitMapSize ();
}

void
CdnServer::SetPacketWindowSize (uint16_t size)
{
  NS_LOG_FUNCTION (this << size);
  m_lossCounter.SetBitMapSize (size);
}

uint32_t
CdnServer::GetLost (void) const
{
  NS_LOG_FUNCTION (this);
  return m_lossCounter.GetLost ();
}

uint32_t
CdnServer::GetReceived (void) const
{
  NS_LOG_FUNCTION (this);
  return m_received;
}

void
CdnServer::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
CdnServer::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (),
                                                   m_port);
      m_socket->Bind (local);
    }

  m_socket->SetRecvCallback (MakeCallback (&CdnServer::HandleRead, this));

  if (m_socket6 == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket6 = Socket::CreateSocket (GetNode (), tid);
      Inet6SocketAddress local = Inet6SocketAddress (Ipv6Address::GetAny (),
                                                   m_port);
      m_socket6->Bind (local);
    }

  m_socket6->SetRecvCallback (MakeCallback (&CdnServer::HandleRead, this));

}

void
CdnServer::StopApplication ()
{
  NS_LOG_FUNCTION (this);

  if (m_socket != 0)
    {
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
}
void CdnServer::ProcessAck(SeqTsHeader RecAck)
{
}
void CdnServer::ProcessAndHandleReceivedPacket(CdnHeader CdnHdr, SeqTsHeader seqTs, SeqTsHeader RecAck, Ptr<Packet> packet, Address from)
{
   
   CdnHeader ToSendCdnHdr;
   SeqTsHeader packetHdr;
   switch(CdnHdr.GetSynType())
     {
       case 0:
         {
           if(m_state==0)
             {
               ToSendCdnHdr.SetSynType(1);
               ToSendCdnHdr.SetFileSize(m_filesize);
               PopulateBuffer();
               m_state=1;
             }
           else
               {
               ToSendCdnHdr.SetSynType(4);
               }
           break;
         }
     case 2:
       {
         if(m_state==1)
           {
             
             m_state=2;
           }
         //handle pure ack!
         ProcessAck(RecAck);
         return;
       }
     case 4:
       {
         
         //finish this part!
         Ptr<Packet> ToSendPacket;
         uint32_t expectedSeq = m_rxBuffer.NextRxSequence ();
         SeqTsHeader Ack;
         if (!m_rxBuffer.Add (packet, seqTs))
         { // Insert failed: No data or RX buffer full
          Ack.SetSeq(m_rxBuffer.NextRxSequence ());
         }
         
         Ack.SetSeq(m_rxBuffer.NextRxSequence ());
         if (expectedSeq < m_rxBuffer.NextRxSequence ())
         {
           //this is where i send the response for those packets that I received!.
            ConsumeData();
         }
         
         Ack.SetTs(seqTs.GetTsInt());
         uint32_t reqNum=CdnHdr.GetReqNumber();
         ToSendPacket=GetChunk(reqNum, ToSendPacket);
         
         ToSendPacket->AddHeader (Ack);
         
         packetHdr.SetSeq(m_sent);
         ToSendPacket->AddHeader (packetHdr);
         ToSendPacket->AddHeader(CdnHdr);
         m_sent++;
         m_socket->SendTo (ToSendPacket, 0, from);
         
         return;  
       }
        
       default:
         {
            ToSendCdnHdr.SetSynType(4);
            break;
         }
     }
     //we haven't checked the ack is in sequence or not, we are assuming no lost packets at the moment, (and no out of orders!).
     Ptr<Packet> ToSendPacket=Create<Packet> (0);
     //inspired by TcpSocketBase, receiving data.
     uint32_t expectedSeq = m_rxBuffer.NextRxSequence ();
     SeqTsHeader Ack;
     if (!m_rxBuffer.Add (packet, seqTs))
      { // Insert failed: No data or RX buffer full
        Ack.SetSeq(m_rxBuffer.NextRxSequence ());
      }
   
     Ack.SetSeq(m_rxBuffer.NextRxSequence ());
     if (expectedSeq < m_rxBuffer.NextRxSequence ())
       {
         ConsumeData();
       }
  
     Ack.SetTs(seqTs.GetTsInt());
     ToSendPacket->AddHeader (Ack);
     packetHdr.SetSeq(m_sent);
     ToSendPacket->AddHeader (packetHdr);
     ToSendPacket->AddHeader(ToSendCdnHdr);
     m_sent++;
     m_socket->SendTo (ToSendPacket, 0, from);
    
  } 

void CdnServer::ConsumeData(void)
{
}

void
CdnServer::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
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
          
          //process the type of packet that was being sent.
          ProcessAndHandleReceivedPacket(cdnhdr, seqTs, AckHdr, packet, from);
          m_received++;
        }
    }

}

void CdnServer::PopulateBuffer(void)
{
  Ptr<Packet> p;
  m_txBuffer.SetHeadSequence (1);
  for(uint32_t i=0; i<m_filesize; i++)
    {
      p=Create<Packet> (m_chunksize);
      m_txBuffer.Add (p);
    }
  
}
  /*gets the relevant chunk of the file from the tx buffer.*/
  Ptr<Packet> CdnServer::GetChunk(uint16_t reqnum, Ptr<Packet> packet)
  {


    packet = m_txBuffer.CopyFromSequence (1, reqnum);
    return packet;
  }

} // Namespace ns3

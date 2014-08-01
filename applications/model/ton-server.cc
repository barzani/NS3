/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
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
 * Author:  Behnaz Arzani <arzanibehnaz@gmail.com>
 */
#include "ns3/address.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ton-server.h"
#include "cdn-header.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TonServer")
  ;
NS_OBJECT_ENSURE_REGISTERED (TonServer)
  ;

TypeId 
TonServer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TonServer")
    .SetParent<Application> ()
    .AddConstructor<TonServer> ()
    .AddAttribute ("Local", "The Address on which to Bind the rx socket.",
                   AddressValue (),
                   MakeAddressAccessor (&TonServer::m_local),
                   MakeAddressChecker ())
    .AddAttribute ("Protocol", "The type id of the protocol to use for the rx socket.",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&TonServer::m_tid),
                   MakeTypeIdChecker ())
    .AddTraceSource ("Rx", "A packet has been received",
                     MakeTraceSourceAccessor (&TonServer::m_rxTrace))
    .AddAttribute ("FileSize", 
                   "The total size of the file ",
                   UintegerValue (30),
                   MakeUintegerAccessor (&TonServer::m_filesize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Speed", 
                   "The speed with which the application can send packets ",
                   IntegerValue (-1),
                   MakeIntegerAccessor (&TonServer::m_speed),
                   MakeIntegerChecker<uint32_t> ())
  ;
  return tid;
}

TonServer::TonServer ()
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_totalRx = 0;
  m_filesize=30;
  m_ismain=false;
  m_txBuffer.SetMaxBufferSize (m_filesize+1);
  m_speed=-1;
  m_chunksize=0;
}

TonServer::~TonServer()
{
  NS_LOG_FUNCTION (this);
}

uint32_t TonServer::GetTotalRx () const
{
  NS_LOG_FUNCTION (this);
  return m_totalRx;
}

Ptr<Socket>
TonServer::GetListeningSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}

std::list<Ptr<Socket> >
TonServer::GetAcceptedSockets (void) const

{
  NS_LOG_FUNCTION (this);
  return m_socketList;
}

void TonServer::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_socketList.clear ();

  // chain up
  Application::DoDispose ();
}


// Application Methods
void TonServer::StartApplication ()    // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);
  // Create the socket if not already
  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), m_tid);
      m_socket->Bind (m_local);
      m_socket->Listen ();
    }
  m_socket->SetRecvCallback (MakeCallback (&TonServer::HandleRead, this));
  m_socket->SetAcceptCallback (
    MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
    MakeCallback (&TonServer::HandleAccept, this));
  m_socket->SetCloseCallbacks (
    MakeCallback (&TonServer::HandlePeerClose, this),
    MakeCallback (&TonServer::HandlePeerError, this));
}

void TonServer::StopApplication ()     // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);
  while(!m_socketList.empty ()) //these are accepted sockets, close them
    {
      Ptr<Socket> acceptedSocket = m_socketList.front ();
      m_socketList.pop_front ();
      acceptedSocket->Close ();
    }
  if (m_socket) 
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
}

void TonServer::DoHandleRead(Ptr<Socket> socket)
{
 NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  CdnHeader cdnhdr;
  
  while ((packet = socket->RecvFrom (from)))
    {
      if (packet->GetSize () == 0)
        { //EOF
          break;
        }
      packet->RemoveHeader(cdnhdr);
      switch(cdnhdr.GetSynType())
        {
        case 0:
          {
            CdnHeader ToSendCdnHdr;
            m_chunksize=cdnhdr.GetFileSize();
            m_txBuffer.SetSize(m_chunksize);
            PopulateBuffer();
            ToSendCdnHdr.SetReqNumber(cdnhdr.GetReqNumber());
            if(m_ismain)
              {
                ToSendCdnHdr.SetSynType(0);
              }
            else
              {
                ToSendCdnHdr.SetSynType(4);
              }
            ToSendCdnHdr.SetFileSize(m_filesize);
            if(m_ismain)
             {

               NS_ASSERT(m_peerAddress.size()==m_peerPort.size());
               for(int j=0;j<m_peerAddress.size();j++)
                 {
                   ToSendCdnHdr.SetDestination(m_peerAddress[j]);
                   ToSendCdnHdr.SetPort(m_peerPort[j]);
                 }
             }
            //ouch haven't added the chunk yet!  
            
            packet=GetChunk(cdnhdr.GetReqNumber(),packet);
            packet->AddHeader(ToSendCdnHdr);
            socket->SendTo(packet, 0, from);
            break;
          }

        default:
          {
            
            CdnHeader ToSendCdnHdr;
            ToSendCdnHdr.SetSynType(4);
            Ptr<Packet> ToSendPacket;
            ToSendCdnHdr.SetReqNumber(cdnhdr.GetReqNumber());
            uint32_t reqNum=cdnhdr.GetReqNumber();
            ToSendPacket=GetChunk(reqNum, ToSendPacket);
            ToSendPacket->AddHeader(ToSendCdnHdr);
            socket->SendTo (ToSendPacket, 0, from); 
            return;  
          }
        }
      
      
      m_totalRx += packet->GetSize ();
     
      m_rxTrace (packet, from);
    }
}


void TonServer::HandleRead (Ptr<Socket> socket)
{
  if(m_speed==(-1) || m_chunksize==0)
    {
      DoHandleRead(socket);
    }
  else
    {
      int32_t time=(m_chunksize*1.0/m_speed);
      Simulator::Schedule (Seconds (time), &TonServer::HandleRead, this, socket);
    }
 
}

void TonServer::HandlePeerClose (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}
 
void TonServer::HandlePeerError (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}
 

void TonServer::HandleAccept (Ptr<Socket> s, const Address& from)

{
  NS_LOG_FUNCTION (this << s << from);
  s->SetRecvCallback (MakeCallback (&TonServer::HandleRead, this));
  m_socketList.push_back (s);
}
/*gets the relevant chunk of the file from the tx buffer.*/
Ptr<Packet> TonServer::GetChunk(uint16_t reqnum, Ptr<Packet> packet)
{
  packet = m_txBuffer.CopyFromSequence (1, reqnum+1);
  return packet;
}
void TonServer::PopulateBuffer(void)
{
  Ptr<Packet> p;
  m_txBuffer.SetHeadSequence (1);

  for(uint32_t i=0; i<m_filesize; i++)
    {
      p=Create<Packet> (m_chunksize);
      m_txBuffer.Add (p);
    }
  
}
void TonServer::ConsumeData()
{}

void
TonServer::AddRemote (Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress.push_back(ip);
  m_peerPort.push_back(port);
}
void 
TonServer::SetMain()
{
  m_ismain=true;
}
} // Namespace ns3

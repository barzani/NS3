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
// Author: Behnaz Arzani <arzanibehna@gmail.com>
//based on code by:
// Author: George F. Riley<riley@ece.gatech.edu>
//

// ns3 - On/Off Data Source Application class
// George F. Riley, Georgia Tech, Spring 2007
// Adapted from ApplicationOnOff in GTNetS.

#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/packet-socket-address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "ton-application.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "cdn-header.h"
#include <vector>
#include <iostream>
#include <fstream>



NS_LOG_COMPONENT_DEFINE ("TonApplication");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (TonApplication)
  ;

TypeId
TonApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TonApplication")
    .SetParent<Application> ()
    .AddConstructor<TonApplication> ()
    .AddAttribute ("DataRate", "The data rate in on state.",
                   DataRateValue (DataRate ("500kb/s")),
                   MakeDataRateAccessor (&TonApplication::m_cbrRate),
                   MakeDataRateChecker ())
    .AddAttribute ("PacketSize", "The size of packets sent in on state",
                   UintegerValue (131072),
                   MakeUintegerAccessor (&TonApplication::m_pktSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("Remote", "The address of the destination",
                   AddressValue (),
                   MakeAddressAccessor (&TonApplication::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("OnTime", "A RandomVariableStream used to pick the duration of the 'On' state.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                   MakePointerAccessor (&TonApplication::m_onTime),
                   MakePointerChecker <RandomVariableStream>())
    .AddAttribute ("OffTime", "A RandomVariableStream used to pick the duration of the 'Off' state.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                   MakePointerAccessor (&TonApplication::m_offTime),
                   MakePointerChecker <RandomVariableStream>())
                
    .AddAttribute ("Protocol", "The type of protocol to use.",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&TonApplication::m_tid),
                   MakeTypeIdChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&TonApplication::m_txTrace))
  ;
  return tid;
}


TonApplication::TonApplication ()
  : m_socket (0),
    m_connected (false),
    m_lastStartTime (Seconds (0))
{
  NS_LOG_FUNCTION (this);
  m_pktSize=131072;
  m_nextTxSequence=0;
  oldReceive=Seconds(0.0);
}

TonApplication::~TonApplication()
{
  NS_LOG_FUNCTION (this);
}



Ptr<Socket>
TonApplication::GetSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}

int64_t 
TonApplication::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_onTime->SetStream (stream);
  m_offTime->SetStream (stream + 1);
  return 2;
}

void
TonApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  m_socket = 0;
  // chain up
  Application::DoDispose ();
}

// Application Methods
void TonApplication::StartApplication () // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  // Create the socket if not already
  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), m_tid);
      if (Inet6SocketAddress::IsMatchingType (m_peer))
        {
          m_socket->Bind6 ();
        }
      else if (InetSocketAddress::IsMatchingType (m_peer) ||
               PacketSocketAddress::IsMatchingType (m_peer))
        {
          m_socket->Bind ();
        }
      m_socket->Connect (m_peer);
      m_socket->SetRecvCallback(MakeCallback (&TonApplication::HandleRead, this));
      m_socket->SetConnectCallback (
        MakeCallback (&TonApplication::ConnectionSucceeded, this),
        MakeCallback (&TonApplication::ConnectionFailed, this));
    }
  m_cbrRateFailSafe = m_cbrRate;

  // If we are not yet connected, there is nothing to do here
  // The ConnectionComplete upcall will start timers at that time
  //if (!m_connected) return;
  m_socket_list.push_back(m_socket);
  Connect(m_socket);
}

void TonApplication::AddNewSubflow(CdnHeader ack)
{
  Ptr<Socket> socket;
  NS_LOG_FUNCTION (this);

  Address address(InetSocketAddress(Ipv4Address::ConvertFrom(ack.GetDestination()),ack.GetPort()));
  // Create the socket if not already
  if (!socket)
    {
      socket = Socket::CreateSocket (GetNode (), m_tid);
      if (Inet6SocketAddress::IsMatchingType (address))
        {
          socket->Bind6 ();
        }
      else if (InetSocketAddress::IsMatchingType (address) ||
               PacketSocketAddress::IsMatchingType (address))
        {
          socket->Bind ();
        }
      socket->Connect (address);
      socket->SetRecvCallback(MakeCallback (&TonApplication::HandleRead, this));
      socket->SetConnectCallback (
        MakeCallback (&TonApplication::ConnectionSucceeded, this),
        MakeCallback (&TonApplication::ConnectionFailed, this));
    }

  // If we are not yet connected, there is nothing to do here
  // The ConnectionComplete upcall will start timers at that time
  //if (!m_connected) return;
  m_socket_list.push_back(socket);
  Connect(socket);
}




void TonApplication::StopApplication () // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  if(m_socket != 0)
    {
      m_socket->Close ();
    }
  else
    {
      NS_LOG_WARN ("TonApplication found null socket to close in StopApplication");
    }
}



void TonApplication::StartSending (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this);
  CdnHeader cdnhdr;
  if (m_txBuffer.SizeFromSequence (m_nextTxSequence) && socket!=m_socket)
    {
       cdnhdr.SetReqNumber(m_nextTxSequence);
       m_highTxMark= std::max (SequenceNumber32(m_nextTxSequence + 1), m_highTxMark.Get ());   
       m_nextTxSequence++;  
    }
  else
    {
      cdnhdr.SetReqNumber(0);
    }
  cdnhdr.SetSynType(0);
  //use this field to transmit the chunk size.


  cdnhdr.SetFileSize(m_pktSize);
  Ptr<Packet> p=Create<Packet> (0);
  p->AddHeader(cdnhdr);
  m_txTrace (p);
  socket->Send (p);
}

void TonApplication::StopSending ()
{
  NS_LOG_FUNCTION (this);

}



void TonApplication::Connect (Ptr<Socket> socket)
{  // Schedules the event to start sending data (switch to the "On" state)
  NS_LOG_FUNCTION (this);
  m_startStopEvent = Simulator::Schedule (Seconds (0.0), &TonApplication::StartSending, this, socket);
}

void TonApplication::ScheduleStopEvent ()
{  // Schedules the event to stop sending data (switch to "Off" state)
  NS_LOG_FUNCTION (this);

  Time onInterval = Seconds (m_onTime->GetValue ());
  NS_LOG_LOGIC ("stop at " << onInterval);
  m_startStopEvent = Simulator::Schedule (onInterval, &TonApplication::StopSending, this);
}




void TonApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  m_connected = true;
}

void TonApplication::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}

void TonApplication::HandleRead (Ptr<Socket> socket)
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
      
      if((uint32_t)(packet->GetSize())<(uint32_t)(m_pktSize+cdnhdr.GetSerializedSize()))
       {      
     
         if(m_halfpackets.count(socket))
          {
            m_halfpackets[socket]->RemoveHeader(cdnhdr);
            m_halfpackets[socket]->AddAtEnd(packet);
            m_halfpackets[socket]->AddHeader(cdnhdr);
            packet=m_halfpackets[socket];
            
            if((uint32_t)(packet->GetSize())<(uint32_t)(m_pktSize+cdnhdr.GetSerializedSize()))
              {
                continue;
              }
            else
              {
                packet->PeekHeader(cdnhdr);
                if((uint32_t)(packet->GetSize()==(uint32_t)(m_pktSize+cdnhdr.GetSerializedSize())))
                  {
                    m_halfpackets.erase(socket);
                  }
                else
                  {
                    Ptr<Packet> temp=packet->CreateFragment(0, m_pktSize-1);
                    m_halfpackets[socket]=packet->CreateFragment(m_pktSize,packet->GetSize()-m_pktSize);
                    packet=temp;
                    NS_ASSERT(((uint32_t)(packet->GetSize()==(uint32_t)(m_pktSize+cdnhdr.GetSerializedSize()))));
                    
                  }
              }
          }
         else
           {
             m_halfpackets[socket]=packet;
             continue;
           }
       }
      
      packet->RemoveHeader(cdnhdr);
      
      switch(cdnhdr.GetSynType())
        {
        case 0:
          {

            CdnHeader ToSendCdnHdr;
            SeqTsHeader tempack;
            tempack.SetSeq(0);
            m_filesize=cdnhdr.GetFileSize();
            m_rxBuffer.SetNextRxSequence(0);
            /*  if (!m_rxBuffer.Add (packet, tempack))
              { // Insert failed: No data or RX buffer full
                NS_ASSERT(0);
                return;
                }*/

            // m_highTxMark=SequenceNumber32(++m_nextTxSequence);
            // m_txBuffer.SetHeadSequence (m_nextTxSequence);
            ProcessAck(packet,cdnhdr);
            PopulateBuffer();
                //Setup transmission list here, later!.
            std::cout<<"going in\n";
             SendNextPacketTo(from, socket); 
             if(cdnhdr.GetNumber()!=0)
               {
                 for(int j=0; j<cdnhdr.GetNumber(); j++)
                   {
                      AddNewSubflow(cdnhdr);
                   }
               }
            break;
          }
        default:
          { 
            //std::cout<<"injast alan dare file size o ava mikone !!!!"<<packet->GetSize()<<"\n";

            if(cdnhdr.GetNumber()!=0)
              {
                 for(int j=0; j<cdnhdr.GetNumber(); j++)
                   {
                     AddNewSubflow(cdnhdr);
                   }
              }
            
           
            ProcessAck(packet,cdnhdr);
            

            SendNextPacketTo(from, socket);
            break;
          }
       }
     }
}

void TonApplication::SendNextPacketTo(Address from, Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this);
  /**
   * Check to see if we have received all needed chunks.
   */
  if(m_rxBuffer.NextRxSequence ()>=m_filesize)
   {
     std::cout<<m_rxBuffer.NextRxSequence ()<<"and "<<m_filesize << " file size\n";
     std::cout<<"Finished file transmit! at time "<<(Simulator::Now()).GetSeconds()<< "\n";
     Simulator::Stop();
   }
  
  if (m_txBuffer.SizeFromSequence (m_nextTxSequence))
   {   
     /**
      * Send this packet on the subflow.
      */
     

     if(!SendDataPacket (from, socket, m_nextTxSequence))
      {
        m_nextTxSequence += 1;                     // Advance next tx sequence
      }  
    }
  else
    {
      Retransmit(from, socket);
    }
}

void TonApplication::Retransmit(Address from, Ptr<Socket> socket)
{
  SendDataPacket (from, socket, m_txBuffer.HeadSequence ());
  // In case of RTO, advance m_nextTxSequence
  m_nextTxSequence = std::max (m_nextTxSequence.Get (), m_txBuffer.HeadSequence () + 1); 
}

uint32_t TonApplication::SendDataPacket(Address from, Ptr<Socket> socket, uint32_t seq)
{
 NS_LOG_FUNCTION (this);
 int32_t num=m_txBuffer.ReturnMaxPossible(1, seq);
 if(num==-1 || num == 0)
  {
    return 1;
  }
 Ptr<Packet> p;
 CdnHeader cdnhdr;
 for(uint32_t i=0; i<num; ++i)
  {  
    p = Create<Packet>(0);
    cdnhdr.SetReqNumber(seq);
    cdnhdr.SetSynType(4);
    p->AddHeader (cdnhdr);
    m_highTxMark= std::max (SequenceNumber32(seq + 1), m_highTxMark.Get ());    
    socket->SendTo (p, 0, from);   
  }  
 return 0;
}
void TonApplication::PopulateBuffer(void)
{
  NS_LOG_FUNCTION (this);
  m_remfromfile=m_filesize;
  uint32_t i;
  /*We add to the buffer the number of bytes that need to be requested.*/
  uint32_t m_lastDataAdded=std::min(m_filesize,m_txBuffer.Available());
  m_remfromfile--;
  for (i=1; i<m_lastDataAdded; i++)
  {
  //I have setup a tx buffer with the number of the packets that I need!
   if(m_txBuffer.Add (i))
    {
      m_remfromfile--;
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
   }
}
void TonApplication::ProcessAck(Ptr<Packet> p, CdnHeader Ack)
{
 NS_LOG_FUNCTION (this);
 // if(Ack.GetPort()!=0)
 // {
 //   AddNewSubflow(Ack);
 // }
 /**
  * We need to get a sequence number
  * for the recieved packet.
  */
  std::FILE *f;
  f = std::fopen("ton.txt", "a");
  fprintf(f, "%f\n",(m_pktSize)/(((Simulator::Now()).GetSeconds()-oldReceive.GetSeconds())*1000.0));
  fflush(f);
  oldReceive=Simulator::Now();
  fclose(f);
  if(p->GetSize()>0)
   {
     SeqTsHeader tempack;
     std::cout<<"got "<<Ack.GetReqNumber()<<"\n";
     tempack.SetSeq(Ack.GetReqNumber());
     if (!m_rxBuffer.Add (p, tempack))
      { // Insert failed: No data or RX buffer full
        std::cout<<"WARNING:buffer full! or no data\n";
        return;
       }
     Ack.SetReqNumber(m_rxBuffer.NextRxSequence ());
      
   }
     /** 
     *First have to check if the ack is in sequence!
     * will have to change this later, to reflect multiple subflows.
     */
  if (Ack.GetReqNumber () < m_txBuffer.HeadSequence ())
   { // Case 1: Old ACK, ignored.
    NS_LOG_LOGIC ("Ignored ack of " << Ack.GetReqNumber());
   }
  else if (Ack.GetReqNumber () == m_txBuffer.HeadSequence ())
   { // Case 2: Potentially a duplicated ACK
     //it was a duplicate ack, and we don't need to do anything?
   }
   else if (Ack.GetReqNumber () > m_txBuffer.HeadSequence ())
    { // Case 3: New ACK, reset m_dupAckCount and update m_txBuffer
      NS_LOG_LOGIC ("New ack of " << Ack.GetReqNumber ());
    }
   uint32_t expectedSeq = m_rxBuffer.NextRxSequence ();
   if (expectedSeq < m_rxBuffer.NextRxSequence ())
     {
       ConsumeData();
     }
   NewAck (SequenceNumber32(Ack.GetReqNumber ()));
}
void 
TonApplication::NewAck (const SequenceNumber32& seq)
{
  NS_LOG_FUNCTION (this);
  uint32_t ack=seq.GetValue();
  if(ack==(m_filesize+1))
   {
     std::cout<<"file completely served!??\n";
     Simulator::Stop();
   }
  DoNewAck (seq);
}
void 
TonApplication::DoNewAck (const SequenceNumber32& seq)
{
  NS_LOG_FUNCTION (this);
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
        if(m_txBuffer.Add (i))
          {
            m_remfromfile--;
          }
       }
    }
  if (ack > m_nextTxSequence)
    { 
      m_nextTxSequence = ack; // If advanced
    }
}
void TonApplication::ConsumeData(void)
{
}
} // Namespace ns3

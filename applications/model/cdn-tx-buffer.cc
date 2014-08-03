/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 Adrian Sai-wah Tam
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
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */

#include <iostream>
#include <algorithm>
#include <cstring>

#include "ns3/packet.h"
#include "ns3/fatal-error.h"
#include "ns3/log.h"

#include "cdn-tx-buffer.h"

NS_LOG_COMPONENT_DEFINE ("CdnTxBuffer");

namespace ns3 {

TypeId
CdnTxBuffer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CdnTxBuffer")
    .SetParent<Object> ()
    .AddConstructor<CdnTxBuffer> ()
    .AddTraceSource ("UnackSequence",
                     "First unacknowledged sequence number (SND.UNA)",
                     MakeTraceSourceAccessor (&CdnTxBuffer::m_firstByteSeq))
  ;
  return tid;
}

/* A user is supposed to create a TcpSocket through a factory. In TcpSocket,
 * there are attributes SndBufSize and RcvBufSize to control the default Tx and
 * Rx window sizes respectively, with default of 128 KiByte. The attribute
 * SndBufSize is passed to TcpTxBuffer by TcpSocketBase::SetSndBufSize() and in
 * turn, TcpTxBuffer:SetMaxBufferSize(). Therefore, the m_maxBuffer value
 * initialized below is insignificant.
 */
CdnTxBuffer::CdnTxBuffer (uint32_t n)
  : m_firstByteSeq (n), m_size (0),m_size_num(0), m_maxBuffer (100000), m_data (0), m_packetsize(1400)
{
}

CdnTxBuffer::~CdnTxBuffer (void)
{
}


void CdnTxBuffer::SetSize(uint32_t size)
{
    m_packetsize=size;
}
uint32_t
CdnTxBuffer::HeadSequence (void) const
{
  return m_firstByteSeq;
}

uint32_t
CdnTxBuffer::TailSequence (void) const
{
  if(m_size_num!=0)
    {
      return m_firstByteSeq +  (m_size_num);
    }
  return m_firstByteSeq +  (m_size);
}

uint32_t
CdnTxBuffer::Size (void) const
{
  if(m_size_num!=0)
    {
      return m_size_num;
    }
  return m_size;
}

uint32_t
CdnTxBuffer::MaxBufferSize (void) const
{
  return m_maxBuffer;
}

void
CdnTxBuffer::SetMaxBufferSize (uint32_t n)
{
  m_maxBuffer = n;
}

uint32_t
CdnTxBuffer::AvailableNum (void) const
{
  //std::cout<<"m_size is "<< m_size_num <<" m_data size is "<< m_datanum.size()<<" and "<<this << "  \n";
  //std::cout<<"availability is "<< m_maxBuffer - m_size_num<< "\n";
      return m_maxBuffer - m_size_num;
  
}
uint32_t
CdnTxBuffer::Available(void) const
{

  return m_maxBuffer - m_size;
}

bool
CdnTxBuffer::Add (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << p);
  NS_LOG_LOGIC ("Packet of size " << p->GetSize () << " appending to window starting at "
                                  << m_firstByteSeq << ", availSize="<< Available ());
  if (1 <= Available ())
    {
      if (p->GetSize () > 0)
        {
          m_data.push_back (p);
          m_size += 1;
          NS_LOG_LOGIC ("Updated size=" << m_size << ", lastSeq=" << m_firstByteSeq +  (m_size));
        }
      return true;
    }
  NS_LOG_LOGIC ("Rejected. Not enough room to buffer packet.");
  return false;
}
bool
CdnTxBuffer::Add (uint32_t num)
{
  
  
  if (1 <= AvailableNum ())
    {
        m_datanum.push_back (num);
        m_size_num += 1;
     
        NS_LOG_LOGIC ("Updated size=" << m_size << ", lastSeq=" << m_firstByteSeq +  (m_size));
        
      return true;
    }
  NS_LOG_LOGIC ("Rejected. Not enough room to buffer number.");
  return false;
}

uint32_t CdnTxBuffer::SizeFromSequence (const uint32_t& seq) const
{
  NS_LOG_FUNCTION (this << seq);
  // Sequence of last byte in buffer
  uint32_t lastSeq;
 
  if(m_size_num!=0)
    {
      lastSeq = m_firstByteSeq +  (m_size_num);
      // std::cout<<"this is m_size_num" <<m_size_num <<"this is m_firstByteSeq "<< m_firstByteSeq<< "number returned is "<< ( lastSeq - seq)<<"\n"; 
    }
  else{
    lastSeq = m_firstByteSeq +  (m_size);
  }
  //std::cout<< "m_size is  "<<m_size << " last seq is "<<lastSeq<< "and first byte is "<<m_firstByteSeq <<"\n";
  // Non-negative size
  NS_LOG_LOGIC ("HeadSeq=" << m_firstByteSeq << ", lastSeq=" << lastSeq << ", size=" << m_size <<
                ", returns " << lastSeq - seq);
  return lastSeq - seq;
}

uint32_t CdnTxBuffer::ReturnMaxPossible(uint32_t numBytes, const uint32_t& seq)
{
  
  uint32_t s = std::min (numBytes, (seq));

    if (s == 0 || (m_datanum.size () == 0 && m_data.size()))
    {
      return -1; // Empty packet returned
    }
    uint32_t count=m_firstByteSeq;
    uint32_t numpackets=0;
  if(m_datanum.size()!=0)
   {

  for(NumBufIterator i = m_datanum.begin(); i != m_datanum.end(); ++i)
    {
     
      if(count+1>seq)
        {
          numpackets++;
          if(numpackets>=s)
            {
              return numpackets;
            }
        }
      count++;
    }
      }
    else
      {
     for(BufIterator i = m_data.begin(); i != m_data.end(); ++i)
    {
      if(count+1>seq)
        {
          numpackets++;
          if(numpackets>=s)
            {
              return numpackets;
            }
        }
      count++;
    }
      }
  return numpackets;
}

Ptr<Packet>
CdnTxBuffer::CopyFromSequence (uint32_t numBytes, const uint32_t& seq)
{

  NS_LOG_FUNCTION (this << numBytes << seq);
  uint32_t s = std::min (numBytes, (seq)); // Real size to extract. Insure not beyond end of data
  
  if (s == 0)
    {
      return Create<Packet> (); // Empty packet returned
    }

  if (m_data.size () == 0)
    { // No actual data, just return dummy-data packet of correct size
      return Create<Packet> (0);
    }
  // Extract data from the buffer and return
  uint32_t offset = seq - m_firstByteSeq.Get ();

  uint32_t count = 0;      // Offset of the first byte of a packet in the buffer
  uint32_t pktSize = 0;
  bool beginFound = false;
  int pktCount = 0;
  Ptr<Packet> outPacket; 
  NS_LOG_LOGIC ("There are " << m_data.size () << " number of packets in buffer");
  for (BufIterator i = m_data.begin (); i != m_data.end (); ++i)
    {
        
      pktCount++;
      pktSize = (*i)->GetSize ();
      if (!beginFound)
        { 
          if (count+1  > offset)
            {              
              NS_LOG_LOGIC ("First byte found in packet #" << pktCount << " at buffer offset " << count
                                                       << ", packet len=" << pktSize);
              beginFound = true;
              uint32_t packetOffset = offset - count;
              uint32_t fragmentLength = count + 1 - offset;
              if (fragmentLength >= s)
                { // Data to be copied falls entirely in this packet
                
                  return (*i)->CreateFragment (packetOffset, s*m_packetsize);
                }
              else
                { // This packet only fulfills part of the request
                  outPacket = (* i)->CreateFragment (packetOffset, fragmentLength*m_packetsize);
                }
              NS_LOG_LOGIC ("Output packet is now of size " << outPacket->GetSize ());
            }          
        }
      else if (count + 1 >= offset + s)
        { // Last packet fragment found
         
          NS_LOG_LOGIC ("Last byte found in packet #" << pktCount << " at buffer offset " << count
                                                      << ", packet len=" << pktSize);
          uint32_t fragmentLength = offset + s - count;
          Ptr<Packet> endFragment = (*i)->CreateFragment (0, fragmentLength*m_packetsize);
          outPacket->AddAtEnd (endFragment);
          NS_LOG_LOGIC ("Output packet is now of size " << outPacket->GetSize ());
          break;
        }
      else
        {
          
          NS_LOG_LOGIC ("Appending to output the packet #" << pktCount << " of offset " << count << " len=" << pktSize);
          outPacket->AddAtEnd (*i);
          NS_LOG_LOGIC ("Output packet is now of size " << outPacket->GetSize ());
        }
      count += 1;
    }

  NS_ASSERT (outPacket->GetSize () == s*m_packetsize); //I tried putting a printf before and after it and the thing would crash before. 
  return outPacket;
}

void
CdnTxBuffer::SetHeadSequence (const uint32_t& seq)
{
  NS_LOG_FUNCTION (this << seq);
  m_firstByteSeq = seq;
}

void
CdnTxBuffer::DiscardUpTo (const uint32_t& seq)
{

  NS_LOG_FUNCTION (this << seq);
  NS_LOG_LOGIC ("current data size=" << m_size << ", headSeq=" << m_firstByteSeq << ", maxBuffer=" << m_maxBuffer
                                     << ", numPkts=" << m_data.size ());
  // Cases do not need to scan the buffer
  if (m_firstByteSeq >= seq) return;

  // Scan the buffer and discard packets
  uint32_t offset = seq - m_firstByteSeq.Get ();  // Number of bytes to remove
  uint32_t pktSize;
  NS_LOG_LOGIC ("Offset=" << offset);
  BufIterator i = m_data.begin ();
 
  //offset=offset*m_packetsize;
  while (i != m_data.end ())
    {

        
      if (offset >= 1)
        { // This packet is behind the seqnum. Remove this packet from the buffer
         
          pktSize = (*i)->GetSize ();
          m_size -= 1;
          offset -= 1; 
          m_firstByteSeq += 1;
          i = m_data.erase (i);
          //  std::cout<<"erased2!!!!!!!!"<<m_data.size()<<"this is "<< this<<"\n";
          NS_LOG_LOGIC ("Removed one packet of size " << pktSize << ", offset=" << offset);
        }
      else if (offset > 0)
        { // Part of the packet is behind the seqnum. Fragment
     
          pktSize = 1 - offset;
          *i = (*i)->CreateFragment (offset*m_packetsize, m_packetsize);
          m_size -= offset;
          m_firstByteSeq += offset;
          NS_LOG_LOGIC ("Fragmented one packet by size " << offset << ", new size=" << pktSize);
          break;
        }
      else
        {

          break;

        }
    }
  // Catching the case of ACKing a FIN
  if (m_size == 0)
    {
      m_firstByteSeq = seq;
    }
  NS_LOG_LOGIC ("size=" << m_size << " headSeq=" << m_firstByteSeq << " maxBuffer=" << m_maxBuffer
                        <<" numPkts="<< m_data.size ());


  NS_ASSERT (m_firstByteSeq == seq);
 
}

void
CdnTxBuffer::DiscardNumUpTo (const uint32_t& seq)
{
  NS_LOG_FUNCTION (this << seq);
  NS_LOG_LOGIC ("current data size=" << m_size << ", headSeq=" << m_firstByteSeq << ", maxBuffer=" << m_maxBuffer
                                     << ", numPkts=" << m_data.size ());
  // Cases do not need to scan the buffer
  if (m_firstByteSeq >= seq) return;

  // Scan the buffer and discard packets
  uint32_t offset = seq - m_firstByteSeq.Get ();  // Number of bytes to remove
  uint32_t pktSize;
  NS_LOG_LOGIC ("Offset=" << offset);
  NumBufIterator i = m_datanum.begin ();
  while (i != m_datanum.end ())
    {
      if (offset >= 1)
        { // This packet is behind the seqnum. Remove this packet from the buffer
          m_size_num -= 1;
          offset -= 1;
          if(m_size==0)
            {
               m_firstByteSeq += 1;
            }
         
          i = m_datanum.erase (i);
          //std::cout<<"erased!!!!!!!!"<<m_datanum.size()<<"this is "<< this <<"\n";
        }
      else if (offset > 0)
        { // Part of the packet is behind the seqnum. Fragment
         
          pktSize = 1 - offset;
          m_size_num -= offset;
          if(m_size==0)
            {
              m_firstByteSeq += offset;
            }
          NS_LOG_LOGIC ("Fragmented one packet by size " << offset << ", new size=" << pktSize);
          break;
        }
      else
        {

          break;

        }
    }
  // Catching the case of ACKing a FIN
  if (m_size == 0)
    {
      m_firstByteSeq = seq;
    }
  NS_LOG_LOGIC ("size=" << m_size << " headSeq=" << m_firstByteSeq << " maxBuffer=" << m_maxBuffer
                        <<" numPkts="<< m_datanum.size ());
  if(m_size==0)
    {
  NS_ASSERT (m_firstByteSeq == seq);
    }

}
} // namepsace ns3

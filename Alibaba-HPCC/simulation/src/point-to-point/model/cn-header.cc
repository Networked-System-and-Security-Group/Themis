/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 New York University
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
 * Author: Adrian S.-W. Tam <adrian.sw.tam@gmail.com>
 */

#include <stdint.h>
#include <iostream>
#include "cn-header.h"
#include "ns3/buffer.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE ("CnHeader");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (CnHeader);

CnHeader::CnHeader (uint16_t pg, uint16_t sport, uint16_t dport)
  : m_pg (pg),
    m_sport (sport),
    m_dport (dport)
{}
CnHeader::CnHeader ()
  : m_pg (0),
    m_sport (0),
    m_dport (0)
{}
CnHeader::~CnHeader ()
{}

void CnHeader::SetPG (uint16_t pg){
  m_pg = pg;
}
void CnHeader::SetDport (uint16_t dport){
  m_dport = dport;
}
void CnHeader::SetSport (uint16_t sport){
  m_sport = sport;
}
uint16_t CnHeader::GetPG (void) const{
  return m_pg;
}
uint16_t CnHeader::GetDport (void) const{
  return m_dport;
}
uint16_t CnHeader::GetSport (void) const{
  return m_sport;
}

TypeId 
CnHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CnHeader")
    .SetParent<Header> ()
    .AddConstructor<CnHeader> ()
    ;
  return tid;
}
TypeId 
CnHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t CnHeader::GetSerializedSize (void)  const
{
  return 6;
}
void CnHeader::Serialize (Buffer::Iterator start)  const
{
  //uint64_t hibyte = m_fid.hi;
  //uint64_t lobyte = m_fid.lo;
  //lobyte = (lobyte & 0x00FFFFFFFFFFFFFFLLU) | (static_cast<uint64_t>(m_qfb)<<56);
  //uint32_t lobyte = (m_qIndex &  0x00FFFFFFLLU) | (static_cast<uint32_t>(m_qfb)<<24);
  //start.WriteU64 (hibyte);
  //start.WriteU64 (lobyte);
  Buffer::Iterator i = start;
  i.WriteU16(m_dport);
  i.WriteU16(m_sport);
  i.WriteU16(m_pg);
  //NS_LOG_LOGIC("CN Seriealized as " << std::hex << hibyte << "+" << lobyte << std::dec);
}

uint32_t CnHeader::Deserialize (Buffer::Iterator start)
{
  //uint64_t hibyte = start.ReadU64 ();
  //uint64_t lobyte = start.ReadU64 ();
  //NS_LOG_LOGIC("CN deseriealized as " << std::hex << hibyte << "+" << lobyte << std::dec);
  //m_qfb = static_cast<uint8_t>(lobyte>>56);
  //m_fid.hi = hibyte;
  //m_fid.lo = lobyte & 0x00FFFFFFFFFFFFFFLLU;
  
  //uint32_t lobyte = start.ReadU32();
  Buffer::Iterator i = start;

  m_dport = i.ReadU16();
  m_sport = i.ReadU16();
  m_pg = i.ReadU16();

  //m_qfb = static_cast<uint8_t>(lobyte>>24);
  //m_qIndex = lobyte & 0x00FFFFFFLLU;

  return GetSerializedSize ();
}
void CnHeader::Print (std::ostream &os) const
{}

}; // namespace ns3

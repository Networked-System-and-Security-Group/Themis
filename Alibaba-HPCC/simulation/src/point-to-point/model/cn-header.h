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

#ifndef CN_HEADER_H
#define CN_HEADER_H

#include <stdint.h>
#include "ns3/header.h"
#include "ns3/buffer.h"

namespace ns3 {

/**
 * \ingroup Cn
 * \brief Header for the Congestion Notification Message
 *
 * This class has two fields: The five-tuple flow id and the quantized
 * congestion level. This can be serialized to or deserialzed from a byte
 * buffer.
 */

class CnHeader : public Header 
{
public:
  CnHeader (uint16_t pg, uint16_t sport, uint16_t dport);
  //CnHeader (const uint16_t fid, uint8_t qIndex, uint8_t qfb);
  CnHeader ();
  virtual ~CnHeader ();

  uint16_t GetPG (void) const;
  uint16_t GetDport (void) const;
  uint16_t GetSport (void) const;

  void SetPG (uint16_t pg);
  void SetDport (uint16_t dport);
  void SetSport (uint16_t sport);

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

private:
  uint16_t m_sport, m_dport;
	uint16_t m_pg;
};

}; // namespace ns3

#endif /* CN_HEADER */

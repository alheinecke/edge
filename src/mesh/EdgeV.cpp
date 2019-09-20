/**
 * @file This file is part of EDGE.
 *
 * @author Alexander Breuer (anbreuer AT ucsd.edu)
 *
 * @section LICENSE
 * Copyright (c) 2019, Alexander Breuer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @section DESCRIPTION
 * Mesh-interface using EDGE-V.
 **/
#include "io/logging.h"

#include "EdgeV.h"
#include "../data/EntityLayout.h"


void edge::mesh::EdgeV::setElLayout( unsigned short         i_nTgs,
                                     std::size_t    const * i_nTgEls,
                                     t_enLayout           & o_elLay ) {
  // assign sizes
  o_elLay.timeGroups.resize( 0 );
  o_elLay.timeGroups.resize( i_nTgs );

  for( unsigned short l_tg = 0; l_tg < i_nTgs; l_tg++ ) {
    o_elLay.timeGroups[l_tg].inner.size = i_nTgEls[l_tg];
#ifdef PP_USE_MPI
    EDGE_LOG_FATAL << "missing comm patterns";
#endif
  }

  // derive the rest
  data::EntityLayout::sizesToLayout( o_elLay );
}


void edge::mesh::EdgeV::setLtsTypes( std::size_t            i_nEls,
                                     unsigned short         i_nElFas,
                                     std::size_t    const * i_elFaEl,
                                     unsigned short         i_nTgs,
                                     std::size_t    const * i_nTgEls,
                                     long long              i_elEq,
                                     long long              i_elLt,
                                     long long              i_elGt,
                                     long long      const * i_adEq,
                                     long long      const * i_adLt,
                                     long long      const * i_adGt,
                                     long long            * o_spTys ) {
  // define lambda which finds the time group of an element
  auto l_getTg = [ i_nTgs, i_nTgEls ]( std::size_t i_el ) {
    std::size_t l_first = 0;

    for( unsigned short l_tg = 0; l_tg < i_nTgs; l_tg++ ) {
      if( i_el >= l_first && i_el < l_first + i_nTgEls[l_tg] ) {
        return l_tg;
      }
      l_first += i_nTgEls[l_tg];
    }

    // fail if we didn't find a time group
    EDGE_LOG_FATAL;
    return std::numeric_limits< unsigned short >::max();
  };

  // iterate over the elements
  for( std::size_t l_el = 0; l_el < i_nEls; l_el++ ) {
    unsigned short l_tgEl = l_getTg( l_el );

    bool l_elEq = false;
    bool l_elLt = false;
    bool l_elGt = false;

    // iterate over face-neighbors
    for( unsigned short l_fa = 0; l_fa < i_nElFas; l_fa++ ) {
      std::size_t l_ad = i_elFaEl[l_el*i_nElFas + l_fa];

      // set GTS at the boundary
      if( l_ad == std::numeric_limits< std::size_t >::max() ) {
        o_spTys[l_el] |= i_adEq[l_fa];
        l_elEq = true;
        continue;
      }

      unsigned short l_tgAd = l_getTg( l_ad );

      // set the LTS relations w.r.t. this face-adjacent element
      if( l_tgEl == l_tgAd ) {
        o_spTys[l_el] |= i_adEq[l_fa];
        l_elEq = true;
      }
      else if( l_tgEl < l_tgAd ) {
        o_spTys[l_el] |= i_adLt[l_fa];
        l_elLt = true;
      }
      else if( l_tgEl > l_tgAd ) {
        o_spTys[l_el] |= i_adGt[l_fa];
        l_elGt = true;
      }
    }

    // set elements LTS configuration
    if( l_elEq ) {
      o_spTys[l_el] |= i_elEq;
    }
    if( l_elLt ) {
      o_spTys[l_el] |= i_elLt;
    }
    if( l_elGt ) {
      o_spTys[l_el] |= i_elGt;
    }
  }
}

edge::mesh::EdgeV::EdgeV( std::string const & i_pathToMesh,
                          bool                i_periodic ): m_moab(i_pathToMesh),
                                                            m_mesh(m_moab, i_periodic) {
  // get tag names
  std::vector< std::string > l_tagNames;
  m_moab.getTagNames( l_tagNames );

  // check if the the mesh is EDGE-V annotated for LTS
  unsigned short l_nLtsTags = 0;
  for( std::size_t l_ta = 0; l_ta < l_tagNames.size(); l_ta++ ) {
    if(      l_tagNames[l_ta] == "edge_v_n_time_group_elements" ) l_nLtsTags++;
    else if( l_tagNames[l_ta] == "edge_v_relative_time_steps"   ) l_nLtsTags++;
  }
  EDGE_CHECK( l_nLtsTags == 0 || l_nLtsTags == 2 );

  // get the LTS info
  m_nTgs = 1;
  if( l_nLtsTags > 0 ) {
    m_nTgs = m_moab.getGlobalDataSize( "edge_v_n_time_group_elements" );
  }
  m_nTgEls = new std::size_t[ m_nTgs ];
  if( l_nLtsTags > 0 ) {
    m_moab.getGlobalData( "edge_v_n_time_group_elements",
                          m_nTgEls );
  }
  else {
    m_nTgEls[0] = m_mesh.nEls();
  }

  // allocate memory for relative time steps and init with GTS
  m_relDt = new double[m_nTgs+1];
  m_relDt[0] = 1;
  m_relDt[1] = std::numeric_limits< double >::max();

  if( m_nTgs > 1 ) {
    // get relative time steps and check for rate-2
    m_moab.getGlobalData( "edge_v_relative_time_steps",
                          m_relDt );

    for( unsigned short l_tg = 0; l_tg < m_nTgs-1; l_tg++ ) {
      double l_rate = m_relDt[l_tg+1] / m_relDt[l_tg];
      EDGE_CHECK_LT( std::abs(l_rate-2.0), 1E-5 );
    }
  }

  setElLayout( m_nTgs,
               m_nTgEls,
               m_elLay );
}

edge::mesh::EdgeV::~EdgeV() {
  delete[] m_nTgEls;
  delete[] m_relDt;
}

void edge::mesh::EdgeV::setLtsTypes( t_elementChars * io_elChars ) const {
  // allocate and init temporary array
  std::size_t l_nEls = m_mesh.nEls();
  long long *l_spTys = new long long[ l_nEls ];
  for( std::size_t l_el = 0; l_el < l_nEls; l_el++ ) l_spTys[l_el] = 0;

  long long l_adEq[6];
  long long l_adLt[6];
  long long l_adGt[6];
  for( unsigned short l_fa = 0; l_fa < 6; l_fa++ ) {
    l_adEq[l_fa] = C_LTS_AD[l_fa][AD_EQ];
    l_adLt[l_fa] = C_LTS_AD[l_fa][AD_LT];
    l_adGt[l_fa] = C_LTS_AD[l_fa][AD_GT];
  }

  // set the LTS types
  edge_v::t_entityType l_elTy = m_moab.getElType();
  unsigned short l_nElFas = edge_v::CE_N_FAS( l_elTy );

  setLtsTypes( l_nEls,
               l_nElFas,
               m_mesh.getElFaEl(),
               m_nTgs,
               m_nTgEls,
               C_LTS_EL[EL_INT_EQ],
               C_LTS_EL[EL_INT_LT],
               C_LTS_EL[EL_INT_GT],
               l_adEq,
               l_adLt,
               l_adGt,
               l_spTys );

  // copy over sparse types
  EDGE_CHECK_EQ( sizeof(long long), sizeof( io_elChars->spType ) );
  for( std::size_t l_el = 0; l_el < l_nEls; l_el++ ) {
    io_elChars[l_el].spType |= l_spTys[l_el];
  }

  delete[] l_spTys;
}
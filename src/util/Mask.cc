// Copyright (C) 2011, 2014, 2015, 2016, 2017, 2018 Constantine Khroulev and David Maxwell
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cassert>

#include "Mask.hh"
#include "IceGrid.hh"

namespace pism {

void GeometryCalculator::compute(const IceModelVec2S& sea_level,
                                 const IceModelVec2S& bed,
                                 const IceModelVec2S& thickness,
                                 IceModelVec2Int& out_mask,
                                 IceModelVec2S& out_surface) const {
  compute_mask(sea_level, bed, thickness, out_mask);
  compute_surface(sea_level, bed, thickness, out_surface);
}

void GeometryCalculator::compute(const IceModelVec2S& sea_level,
                                 const IceModelVec2S& bed,
                                 const IceModelVec2S& thickness,
                                 const IceModelVec2S& lake_level,
                                 IceModelVec2Int& out_mask,
                                 IceModelVec2S& out_surface) const {
  compute_mask(sea_level, bed, thickness, lake_level, out_mask);
  compute_surface(sea_level, bed, thickness, lake_level, out_surface);
}

void GeometryCalculator::compute(const IceModelVec2S& sea_level,
                                 const IceModelVec2S& bed,
                                 const IceModelVec2S& thickness,
                                 const IceModelVec2S& lake_level,
                                 IceModelVec2Int& out_mask,
                                 IceModelVec2S& out_surface,
                                 IceModelVec2Int& out_lake_mask) const {
  compute_mask(sea_level, bed, thickness, lake_level, out_mask);
  compute_surface(sea_level, bed, thickness, lake_level, out_surface);
  compute_lake_mask(lake_level, out_lake_mask);
}

void GeometryCalculator::compute_mask(const IceModelVec2S &sea_level,
                                      const IceModelVec2S &bed,
                                      const IceModelVec2S &thickness,
                                      IceModelVec2Int &result) const {
  IceModelVec::AccessList list{&sea_level, &bed, &thickness, &result};

  const IceGrid &grid = *bed.grid();

  const unsigned int stencil = result.stencil_width();
  assert(sea_level.stencil_width() >= stencil);
  assert(bed.stencil_width()       >= stencil);
  assert(thickness.stencil_width() >= stencil);

  for (PointsWithGhosts p(grid, stencil); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i,j) = this->mask(sea_level(i, j), bed(i, j), thickness(i, j), m_fill_value);
  }
}

void GeometryCalculator::compute_mask(const IceModelVec2S &sea_level,
                                      const IceModelVec2S &bed,
                                      const IceModelVec2S &thickness,
                                      const IceModelVec2S &lake_level,
                                      IceModelVec2Int &result) const {
  IceModelVec::AccessList list{&sea_level, &bed, &thickness, &lake_level, &result};

  const IceGrid &grid = *bed.grid();

  const unsigned int stencil = result.stencil_width();
  assert(sea_level.stencil_width()  >= stencil);
  assert(bed.stencil_width()        >= stencil);
  assert(thickness.stencil_width()  >= stencil);
  assert(lake_level.stencil_width() >= stencil);

  for (PointsWithGhosts p(grid, stencil); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i,j) = this->mask(sea_level(i, j), bed(i, j), thickness(i, j), lake_level(i, j));
  }
}

void GeometryCalculator::compute_surface(const IceModelVec2S &sea_level,
                                         const IceModelVec2S &bed,
                                         const IceModelVec2S &thickness,
                                         IceModelVec2S &result) const {
  IceModelVec::AccessList list{&sea_level, &bed, &thickness, &result};

  const IceGrid &grid = *bed.grid();

  const unsigned int stencil = result.stencil_width();
  assert(sea_level.stencil_width() >= stencil);
  assert(bed.stencil_width()       >= stencil);
  assert(thickness.stencil_width() >= stencil);

  for (PointsWithGhosts p(grid, stencil); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i,j) = this->surface(sea_level(i, j), bed(i, j), thickness(i, j), m_fill_value);
  }
}

void GeometryCalculator::compute_surface(const IceModelVec2S &sea_level,
                                         const IceModelVec2S &bed,
                                         const IceModelVec2S &thickness,
                                         const IceModelVec2S &lake_level,
                                         IceModelVec2S &result) const {
  IceModelVec::AccessList list{&sea_level, &bed, &thickness, &lake_level, &result};

  const IceGrid &grid = *bed.grid();

  const unsigned int stencil = result.stencil_width();
  assert(sea_level.stencil_width()  >= stencil);
  assert(bed.stencil_width()        >= stencil);
  assert(thickness.stencil_width()  >= stencil);
  assert(lake_level.stencil_width() >= stencil);

  for (PointsWithGhosts p(grid, stencil); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i,j) = this->surface(sea_level(i, j), bed(i, j), thickness(i, j), lake_level(i, j));
  }
}

void GeometryCalculator::compute_lake_mask(const IceModelVec2S &lake_level,
                                           IceModelVec2Int &result) const {
  IceModelVec::AccessList list{&lake_level, &result};

  const IceGrid &grid = *lake_level.grid();

  const unsigned int stencil = result.stencil_width();
  assert(lake_level.stencil_width() >= stencil);

  for (PointsWithGhosts p(grid, stencil); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i,j) = this->lake_mask(lake_level(i, j));
  }
}

int GeometryCalculator::mask(double sea_level, double bed, double thickness) const{
  return mask(sea_level, bed, thickness, m_fill_value);
}

double GeometryCalculator::surface(double sea_level, double bed, double thickness) const{
  return surface(sea_level, bed, thickness, m_fill_value);
}

} // end of namespace pism

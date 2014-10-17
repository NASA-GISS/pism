// Copyright (C) 2011, 2014 David Maxwell
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

#include "pism_python.hh"
#include "petsc.h"
#include "pism_python_signal.hh"
#include "pism_const.hh"

using namespace pism;

PetscErrorCode globalMax(double local_max, double *result, MPI_Comm comm)
{
  return GlobalMax(comm, &local_max,  result);
}

PetscErrorCode globalMin(double local_min, double *result, MPI_Comm comm)
{
  return GlobalMin(comm, &local_min,  result);
}

PetscErrorCode globalSum(double local_sum, double *result, MPI_Comm comm)
{
  return GlobalSum(comm, &local_sum,  result);
}

PetscErrorCode optionsGroupBegin(MPI_Comm comm, const std::string &prefix,
                                 const std::string &mess, const std::string &sec)
{
  PetscOptionsPublishCount=(PetscOptionsPublish ? -1 : 1);
  return PetscOptionsBegin_Private(comm, prefix.c_str(), mess.c_str(), sec.c_str());
}

void optionsGroupNext()
{
  PetscOptionsPublishCount++;
}

bool optionsGroupContinue()
{
  return PetscOptionsPublishCount < 2;
}

PetscErrorCode optionsGroupEnd()
{
  return PetscOptionsEnd_Private();
}


void set_abort_on_sigint(bool abort)
{
  gSIGINT_is_fatal = abort;
}

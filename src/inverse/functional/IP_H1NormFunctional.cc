// Copyright (C) 2012, 2014, 2015, 2016, 2017, 2020, 2022, 2023, 2025  David Maxwell and Constantine Khroulev
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

#include "pism/inverse/functional/IP_H1NormFunctional.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/Grid.hh"
#include "pism/util/pism_utilities.hh"
#include "pism/util/array/Scalar.hh"
#include "pism/util/fem/DirichletData.hh"

namespace pism {
namespace inverse {

void IP_H1NormFunctional2S::valueAt(array::Scalar &x, double *OUTPUT) {

  const unsigned int Nk     = fem::q1::n_chi;
  const unsigned int Nq     = m_element.n_pts();
  const unsigned int Nq_max = fem::MAX_QUADRATURE_SIZE;

  // The value of the objective
  double value = 0;

  double x_e[Nk];
  double x_q[Nq_max], dxdx_q[Nq_max], dxdy_q[Nq_max];
  array::AccessScope list(x);

  fem::DirichletData_Scalar dirichletBC(m_dirichletIndices, NULL);

  // Loop through all LOCAL elements.
  const int
    xs = m_element_index.lxs,
    xm = m_element_index.lxm,
    ys = m_element_index.lys,
    ym = m_element_index.lym;

  for (int j=ys; j<ys+ym; j++) {
    for (int i=xs; i<xs+xm; i++) {
      m_element.reset(i, j);

      // Obtain values of x at the quadrature points for the element.
      m_element.nodal_values(x.array(), x_e);
      if (dirichletBC) {
        dirichletBC.enforce_homogeneous(m_element, x_e);
      }
      m_element.evaluate(x_e, x_q, dxdx_q, dxdy_q);

      for (unsigned int q=0; q<Nq; q++) {
        auto W = m_element.weight(q);
        value += W*(m_cL2*x_q[q]*x_q[q]+ m_cH1*(dxdx_q[q]*dxdx_q[q]+dxdy_q[q]*dxdy_q[q]));
      } // q
    } // j
  } // i

  GlobalSum(m_grid->com, &value, OUTPUT, 1);
}

void IP_H1NormFunctional2S::dot(array::Scalar &a, array::Scalar &b, double *OUTPUT) {

  const unsigned int Nk     = fem::q1::n_chi;
  const unsigned int Nq     = m_element.n_pts();
  const unsigned int Nq_max = fem::MAX_QUADRATURE_SIZE;

  // The value of the objective
  double value = 0;

  double a_e[Nk];
  double a_q[Nq_max], dadx_q[Nq_max], dady_q[Nq_max];

  double b_e[Nk];
  double b_q[Nq_max], dbdx_q[Nq_max], dbdy_q[Nq_max];

  array::AccessScope list{&a, &b};

  fem::DirichletData_Scalar dirichletBC(m_dirichletIndices, NULL);

  // Loop through all LOCAL elements.
  const int
    xs = m_element_index.lxs,
    xm = m_element_index.lxm,
    ys = m_element_index.lys,
    ym = m_element_index.lym;

  for (int j=ys; j<ys+ym; j++) {
    for (int i=xs; i<xs+xm; i++) {
      m_element.reset(i, j);

      // Obtain values of x at the quadrature points for the element.
      m_element.nodal_values(a.array(), a_e);
      if (dirichletBC) {
        dirichletBC.enforce_homogeneous(m_element, a_e);
      }
      m_element.evaluate(a_e, a_q, dadx_q, dady_q);

      m_element.nodal_values(b.array(), b_e);
      if (dirichletBC) {
        dirichletBC.enforce_homogeneous(m_element, b_e);
      }
      m_element.evaluate(b_e, b_q, dbdx_q, dbdy_q);

      for (unsigned int q=0; q<Nq; q++) {
        auto W = m_element.weight(q);
        value += W*(m_cL2*a_q[q]*b_q[q]+ m_cH1*(dadx_q[q]*dbdx_q[q]+dady_q[q]*dbdy_q[q]));
      } // q
    } // j
  } // i

  GlobalSum(m_grid->com, &value, OUTPUT, 1);
}


void IP_H1NormFunctional2S::gradientAt(array::Scalar &x, array::Scalar &gradient) {

  const unsigned int Nk     = fem::q1::n_chi;
  const unsigned int Nq     = m_element.n_pts();
  const unsigned int Nq_max = fem::MAX_QUADRATURE_SIZE;

  // Clear the gradient before doing anything with it!
  gradient.set(0);

  double x_e[Nk];
  double x_q[Nq_max], dxdx_q[Nq_max], dxdy_q[Nq_max];

  double gradient_e[Nk];

  array::AccessScope list{&x, &gradient};

  fem::DirichletData_Scalar dirichletBC(m_dirichletIndices, NULL);

  // Loop through all local and ghosted elements.
  const int
    xs = m_element_index.xs,
    xm = m_element_index.xm,
    ys = m_element_index.ys,
    ym = m_element_index.ym;

  for (int j=ys; j<ys+ym; j++) {
    for (int i=xs; i<xs+xm; i++) {

      // Reset the DOF map for this element.
      m_element.reset(i, j);

      // Obtain values of x at the quadrature points for the element.
      m_element.nodal_values(x.array(), x_e);
      if (dirichletBC) {
        dirichletBC.constrain(m_element);
        dirichletBC.enforce_homogeneous(m_element, x_e);
      }
      m_element.evaluate(x_e, x_q, dxdx_q, dxdy_q);

      // Zero out the element-local residual in prep for updating it.
      for (unsigned int k=0; k<Nk; k++) {
        gradient_e[k] = 0;
      }

      for (unsigned int q=0; q<Nq; q++) {
        auto W = m_element.weight(q);
        const double &x_qq=x_q[q];
        const double &dxdx_qq=dxdx_q[q], &dxdy_qq=dxdy_q[q];
        for (unsigned int k=0; k<Nk; k++) {
          gradient_e[k] += 2*W*(m_cL2*x_qq*m_element.chi(q, k).val +
                                   m_cH1*(dxdx_qq*m_element.chi(q, k).dx + dxdy_qq*m_element.chi(q, k).dy));
        } // k
      } // q
      m_element.add_contribution(gradient_e, gradient.array());
    } // j
  } // i
}

void IP_H1NormFunctional2S::assemble_form(Mat form) {

  const unsigned int Nk = fem::q1::n_chi;
  const unsigned int Nq = m_element.n_pts();

  PetscErrorCode ierr;

  // Zero out the Jacobian in preparation for updating it.
  ierr = MatZeroEntries(form);
  PISM_CHK(ierr, "MatZeroEntries");

  fem::DirichletData_Scalar zeroLocs(m_dirichletIndices, NULL);

  // Loop through all the elements.
  const int
    xs = m_element_index.xs,
    xm = m_element_index.xm,
    ys = m_element_index.ys,
    ym = m_element_index.ym;

  ParallelSection loop(m_grid->com);
  try {
    for (int j=ys; j<ys+ym; j++) {
      for (int i=xs; i<xs+xm; i++) {
        // Element-local Jacobian matrix (there are Nk vector valued degrees
        // of freedom per elment, for a total of (2*Nk)*(2*Nk) = 16
        // entries in the local Jacobian.
        double K[Nk][Nk];

        // Initialize the map from global to local degrees of freedom for this element.
        m_element.reset(i, j);

        // Don't update rows/cols where we project to zero.
        if (zeroLocs) {
          zeroLocs.constrain(m_element);
        }

        // Build the element-local Jacobian.
        ierr = PetscMemzero(K, sizeof(K));
        PISM_CHK(ierr, "PetscMemzero");

        for (unsigned int q=0; q<Nq; q++) {
          auto W = m_element.weight(q);
          for (unsigned int k = 0; k < Nk; k++) {   // Test functions
            const fem::Germ &test_qk = m_element.chi(q, k);
            for (unsigned int l = 0; l < Nk; l++) { // Trial functions
              const fem::Germ &test_ql=m_element.chi(q, l);
              K[k][l] += W*(m_cL2*test_qk.val*test_ql.val +
                               m_cH1*(test_qk.dx*test_ql.dx +
                                      test_qk.dy*test_ql.dy));
            } // l
          } // k
        } // q
        m_element.add_contribution(&K[0][0], form);
      } // j
    } // i
  } catch (...) {
    loop.failed();
  }
  loop.check();

  if (zeroLocs) {
    zeroLocs.fix_jacobian(form);
  }

  ierr = MatAssemblyBegin(form, MAT_FINAL_ASSEMBLY);
  PISM_CHK(ierr, "MatAssemblyBegin");

  ierr = MatAssemblyEnd(form, MAT_FINAL_ASSEMBLY);
  PISM_CHK(ierr, "MatAssemblyEnd");
}

} // end of namespace inverse
} // end of namespace pism

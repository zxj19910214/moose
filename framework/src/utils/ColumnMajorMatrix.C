//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

// MOOSE includes
#include "ColumnMajorMatrix.h"

#include "libmesh/petsc_macro.h"

// PETSc includes
#include <petscsys.h>
#include <petscblaslapack.h>

#if !defined(LIBMESH_HAVE_PETSC)
extern "C" void FORTRAN_CALL(dsyev)(...);
extern "C" void FORTRAN_CALL(dgeev)(...);
extern "C" void FORTRAN_CALL(dgetrf)(...);
#endif
#if !defined(LIBMESH_HAVE_PETSC) || PETSC_VERSION_LESS_THAN(3, 5, 0)
extern "C" void FORTRAN_CALL(dgetri)(...); // matrix inversion routine from LAPACK
#endif

ColumnMajorMatrix::ColumnMajorMatrix(unsigned int rows, unsigned int cols)
  : _n_rows(rows), _n_cols(cols), _n_entries(rows * cols), _values(rows * cols, 0.0)
{
  _values.resize(rows * cols);
}

ColumnMajorMatrix::ColumnMajorMatrix(const ColumnMajorMatrix & rhs)
  : _n_rows(LIBMESH_DIM), _n_cols(LIBMESH_DIM), _n_entries(_n_cols * _n_cols)
{
  *this = rhs;
}

ColumnMajorMatrix::ColumnMajorMatrix(const TypeTensor<Real> & rhs)
  : _n_rows(LIBMESH_DIM),
    _n_cols(LIBMESH_DIM),
    _n_entries(LIBMESH_DIM * LIBMESH_DIM),
    _values(LIBMESH_DIM * LIBMESH_DIM)
{
  for (unsigned int j = 0; j < LIBMESH_DIM; ++j)
    for (unsigned int i = 0; i < LIBMESH_DIM; ++i)
      (*this)(i, j) = rhs(i, j);
}

ColumnMajorMatrix::ColumnMajorMatrix(const DenseMatrix<Real> & rhs)
  : _n_rows(LIBMESH_DIM), _n_cols(LIBMESH_DIM), _n_entries(_n_cols * _n_cols)
{
  *this = rhs;
}

ColumnMajorMatrix::ColumnMajorMatrix(const DenseVector<Real> & rhs)
  : _n_rows(LIBMESH_DIM), _n_cols(LIBMESH_DIM), _n_entries(_n_cols * _n_cols)
{
  *this = rhs;
}

ColumnMajorMatrix::ColumnMajorMatrix(const TypeVector<Real> & col1,
                                     const TypeVector<Real> & col2,
                                     const TypeVector<Real> & col3)
  : _n_rows(LIBMESH_DIM),
    _n_cols(LIBMESH_DIM),
    _n_entries(LIBMESH_DIM * LIBMESH_DIM),
    _values(LIBMESH_DIM * LIBMESH_DIM)
{
  unsigned int entry = 0;
  for (unsigned int i = 0; i < LIBMESH_DIM; i++)
    _values[entry++] = col1(i);

  for (unsigned int i = 0; i < LIBMESH_DIM; i++)
    _values[entry++] = col2(i);

  for (unsigned int i = 0; i < LIBMESH_DIM; i++)
    _values[entry++] = col3(i);
}

ColumnMajorMatrix
ColumnMajorMatrix::kronecker(const ColumnMajorMatrix & rhs) const
{
  mooseAssert(_n_rows == rhs._n_cols && _n_cols == rhs._n_rows,
              "Matrices must be the same shape for a kronecker product!");

  ColumnMajorMatrix ret_matrix(_n_rows * _n_rows, rhs._n_cols * rhs._n_cols);

  for (unsigned int i = 0; i < _n_rows; i++)
    for (unsigned int j = 0; j < _n_cols; j++)
      for (unsigned int k = 0; k < rhs._n_rows; k++)
        for (unsigned int l = 0; l < rhs._n_cols; l++)
          ret_matrix(((i * _n_rows) + k), ((j * _n_cols) + l)) = (*this)(i, j) * rhs(k, l);

  return ret_matrix;
}

ColumnMajorMatrix &
ColumnMajorMatrix::operator=(const DenseMatrix<Real> & rhs)
{
  mooseAssert(_n_rows == rhs.m(), "different number of rows");
  mooseAssert(_n_cols == rhs.n(), "different number of cols");

  _n_rows = rhs.m();
  _n_cols = rhs.n();
  _n_entries = rhs.m() * rhs.n();
  _values.resize(rhs.m() * rhs.n());

  for (unsigned int j = 0; j < _n_cols; ++j)
    for (unsigned int i = 0; i < _n_rows; ++i)
      (*this)(i, j) = rhs(i, j);

  return *this;
}

ColumnMajorMatrix &
ColumnMajorMatrix::operator=(const DenseVector<Real> & rhs)
{
  mooseAssert(_n_rows == rhs.size(), "different number of rows");
  mooseAssert(_n_cols == 1, "different number of cols");

  _n_rows = rhs.size();
  _n_cols = 1;
  _n_entries = rhs.size();
  _values.resize(rhs.size());

  for (unsigned int i = 0; i < _n_rows; ++i)
    (*this)(i) = rhs(i);

  return *this;
}

void
ColumnMajorMatrix::eigen(ColumnMajorMatrix & eval, ColumnMajorMatrix & evec) const
{
  mooseAssert(_n_rows == _n_cols, "Cannot solve eigen system of a non-square matrix!");

  char jobz = 'V';
  char uplo = 'U';
  int n = _n_rows;
  int return_value = 0;

  eval._n_rows = _n_rows;
  eval._n_cols = 1;
  eval._n_entries = _n_rows;
  eval._values.resize(_n_rows);

  evec = *this;

  Real * eval_data = eval.rawData();
  Real * evec_data = evec.rawData();

  int buffer_size = n * 64;
  std::vector<Real> buffer(buffer_size);

#if !defined(LIBMESH_HAVE_PETSC)
  FORTRAN_CALL(dsyev)
  (&jobz, &uplo, &n, evec_data, &n, eval_data, &buffer[0], &buffer_size, &return_value);
#else
  LAPACKsyev_(&jobz, &uplo, &n, evec_data, &n, eval_data, &buffer[0], &buffer_size, &return_value);
#endif

  if (return_value)
    mooseError("error in lapack eigen solve");
}

void
ColumnMajorMatrix::eigenNonsym(ColumnMajorMatrix & eval_real,
                               ColumnMajorMatrix & eval_img,
                               ColumnMajorMatrix & evec_right,
                               ColumnMajorMatrix & evec_left) const
{
  mooseAssert(_n_rows == _n_cols, "Cannot solve eigen system of a non-square matrix!");

  ColumnMajorMatrix a(*this);

  char jobvl = 'V';
  char jobvr = 'V';
  int n = _n_rows;
  int return_value = 0;

  eval_real._n_rows = _n_rows;
  eval_real._n_cols = 1;
  eval_real._n_entries = _n_rows;
  eval_real._values.resize(_n_rows);

  eval_img._n_rows = _n_rows;
  eval_img._n_cols = 1;
  eval_img._n_entries = _n_rows;
  eval_img._values.resize(_n_rows);

  Real * a_data = a.rawData();
  Real * eval_r = eval_real.rawData();
  Real * eval_i = eval_img.rawData();
  Real * evec_ri = evec_right.rawData();
  Real * evec_le = evec_left.rawData();

  int buffer_size = n * 64;
  std::vector<Real> buffer(buffer_size);

#if !defined(LIBMESH_HAVE_PETSC)
  FORTRAN_CALL(dgeev)
  (&jobvl,
   &jobvr,
   &n,
   a_data,
   &n,
   eval_r,
   eval_i,
   evec_le,
   &n,
   evec_ri,
   &n,
   &buffer[0],
   &buffer_size,
   &return_value);
#else
  LAPACKgeev_(&jobvl,
              &jobvr,
              &n,
              a_data,
              &n,
              eval_r,
              eval_i,
              evec_le,
              &n,
              evec_ri,
              &n,
              &buffer[0],
              &buffer_size,
              &return_value);
#endif

  if (return_value)
    mooseError("error in lapack eigen solve");
}

void
ColumnMajorMatrix::exp(ColumnMajorMatrix & z) const
{
  mooseAssert(_n_rows == _n_cols, "The Matrix being exponentiated is not square");
  ColumnMajorMatrix a(*this);
  ColumnMajorMatrix evals_real(_n_rows, 1), evals_img(_n_rows, 1), evals_real2(_n_rows, _n_cols);
  ColumnMajorMatrix evec_right(_n_rows, _n_cols), evec_left(_n_rows, _n_cols);
  ColumnMajorMatrix evec_right_inverse(_n_rows, _n_cols);

  a.eigenNonsym(evals_real, evals_img, evec_right, evec_left);

  for (unsigned int i = 0; i < _n_rows; i++)
    evals_real2(i, i) = std::exp(evals_real(i, 0));

  evec_right.inverse(evec_right_inverse);

  z = evec_right * evals_real2 * evec_right_inverse;
}

void
ColumnMajorMatrix::inverse(ColumnMajorMatrix & invA) const
{
  mooseAssert(_n_rows == _n_cols, "Cannot solve for inverse of a non-square matrix!");
  mooseAssert(_n_rows == invA._n_cols && _n_cols == invA._n_rows,
              "Matrices must be the same size for matrix inverse!");

  int n = _n_rows;
  int return_value = 0;

  invA = *this;

  std::vector<PetscBLASInt> ipiv(n);
  Real * invA_data = invA.rawData();

  int buffer_size = n * 64;
  std::vector<Real> buffer(buffer_size);

#if !defined(LIBMESH_HAVE_PETSC)
  FORTRAN_CALL(dgetrf)(&n, &n, invA_data, &n, &ipiv[0], &return_value);
#else
  LAPACKgetrf_(&n, &n, invA_data, &n, &ipiv[0], &return_value);
#endif

#if !defined(LIBMESH_HAVE_PETSC) || PETSC_VERSION_LESS_THAN(3, 5, 0)
  FORTRAN_CALL(dgetri)(&n, invA_data, &n, &ipiv[0], &buffer[0], &buffer_size, &return_value);
#else
  LAPACKgetri_(&n, invA_data, &n, &ipiv[0], &buffer[0], &buffer_size, &return_value);
#endif

  if (return_value)
    mooseException("Error in LAPACK matrix-inverse calculation");
}

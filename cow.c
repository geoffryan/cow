

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COW_PRIVATE_DEFS
#include "cow.h"

void test_trans(double *result, double **args, int **strides,
                void *udata)
{
  printf("in test_trans\n");
}

// -----------------------------------------------------------------------------
//
// private helper functions
//
// -----------------------------------------------------------------------------
#if (COW_MPI)
static void _domain_maketags1d(cow_domain *d);
static void _domain_maketags2d(cow_domain *d);
static void _domain_maketags3d(cow_domain *d);
static void _domain_alloctags(cow_domain *d);
static void _domain_freetags(cow_domain *d);
static void _dfield_maketype1d(cow_dfield *f);
static void _dfield_maketype2d(cow_dfield *f);
static void _dfield_maketype3d(cow_dfield *f);
static void _dfield_alloctype(cow_dfield *f);
static void _dfield_freetype(cow_dfield *f);
#endif
static void _dfield_extractreplace(cow_dfield *f, const int *I0, const int *I1,
                                   void *out, char op);

void cow_init(int argc, char **argv, int modes)
{
#if (COW_MPI)
  int rank = 0;
  int size = 1;
  int mpi_started;
  MPI_Initialized(&mpi_started);
  if (!mpi_started && !(modes & COW_DISABLE_MPI)) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
  }
  if (rank != 0 && !(modes & COW_NOREOPEN_STDOUT)) {
    freopen("/dev/null", "w", stdout);
  }
  MPI_Initialized(&mpi_started);
  if (mpi_started) {
    printf("[cow] MPI is now up and running on %d processes\n", size);
  }
  else {
    printf("[cow] MPI suspended\n");
  }
#endif
}
void cow_finalize(void)
{
#if (COW_MPI)
  int mpi_started;
  MPI_Initialized(&mpi_started);
  if (mpi_started) {
    MPI_Finalize();
  }
#endif
}
int cow_mpirunning(void)
{
  int mpi_started = 0;
#if (COW_MPI)
  MPI_Initialized(&mpi_started);
#endif
  return mpi_started;
}

// -----------------------------------------------------------------------------
//
// cow_domain interface functions
//
// -----------------------------------------------------------------------------
struct cow_domain *cow_domain_new()
{
  cow_domain *d = (cow_domain*) malloc(sizeof(cow_domain));
  cow_domain dom = {
    .glb_lower = { 0.0, 0.0, 0.0 },
    .glb_upper = { 1.0, 1.0, 1.0 },
    .loc_lower = { 0.0, 0.0, 0.0 },
    .loc_upper = { 1.0, 1.0, 1.0 },
    .dx = { 1.0, 1.0, 1.0 },
    .L_nint = { 1, 1, 1 },
    .L_ntot = { 1, 1, 1 },
    .L_strt = { 0, 0, 0 },
    .G_ntot = { 1, 1, 1 },
    .G_strt = { 0, 0, 0 },
    .n_dims = 1,
    .n_ghst = 0,
    .balanced = 1,
    .committed = 0,
#if (COW_MPI)
    .proc_sizes = { 0, 0, 0 },
    .proc_index = { 0, 0, 0 },
#endif
  } ;
  *d = dom;
  return d;
}
void cow_domain_del(cow_domain *d)
{
#if (COW_MPI)
  if (cow_mpirunning()) {
    MPI_Comm_free(&d->mpi_cart);
    _domain_freetags(d);
  }
#endif
#if (COW_HDF5)
  _io_domain_del(d);
#endif
  free(d);
}
void cow_domain_setsize(cow_domain *d, int dim, int size)
{
  if (dim >= 3 || d->committed) return;
  d->G_ntot[dim] = size;
}
int cow_domain_getndim(cow_domain *d)
{
  return d->n_dims;
}
void cow_domain_setndim(cow_domain *d, int ndim)
{
  if (ndim > 3 || d->committed) return;
  d->n_dims = ndim;
}
void cow_domain_setguard(cow_domain *d, int guard)
{
  if (guard < 0 || d->committed) return;
  d->n_ghst = guard;
}
int cow_domain_getguard(cow_domain *d)
{
  return d->n_ghst;
}
void cow_domain_setprocsizes(cow_domain *d, int dim, int size)
{
#if (COW_MPI)
  if (dim >= 3 || d->committed) return;
  d->proc_sizes[dim] = size;
#endif
}
void cow_domain_commit(cow_domain *d)
{
  if (d->committed) return;
#if (COW_MPI)
  if (cow_mpirunning()) {
    int w[3] = { 1, 1, 1 }; // 'wrap', periodic in all directions
    int r = 1; // 'reorder' allow MPI to choose a cart_rank != comm_rank

    MPI_Comm_rank(MPI_COMM_WORLD, &d->comm_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &d->comm_size);
    MPI_Dims_create(d->comm_size, d->n_dims, d->proc_sizes);
    MPI_Cart_create(MPI_COMM_WORLD, d->n_dims, d->proc_sizes, w, r,
                    &d->mpi_cart);
    MPI_Comm_rank(d->mpi_cart, &d->cart_rank);
    MPI_Comm_size(d->mpi_cart, &d->cart_size);
    MPI_Cart_coords(d->mpi_cart, d->cart_rank, d->n_dims, d->proc_index);

    for (int i=0; i<d->n_dims; ++i) {
      // -----------------------------------------------------------------------
      // The number of subgrid zones for dimension i needs to be non-uniform if
      // proc_sizes[i] does not divide the G_ntot[i]. For each dimension, we add
      // a zone to the first R subgrids , where R is given below:
      // -----------------------------------------------------------------------
      const int R = d->G_ntot[i] % d->proc_sizes[i];
      const int normal_size = d->G_ntot[i] / d->proc_sizes[i];
      const int augmnt_size = normal_size + 1;
      const int thisdm_size = (d->proc_index[i]<R) ? augmnt_size : normal_size;
      const double dx = (d->glb_upper[i] - d->glb_lower[i]) / d->G_ntot[i];

      d->dx[i] = dx;
      if (R != 0) d->balanced = 0;

      d->L_nint[i] = thisdm_size;
      d->G_strt[i] = 0;
      for (int j=0; j<d->proc_index[i]; ++j) {
        d->G_strt[i] += (j<R) ? augmnt_size : normal_size;
      }
      d->loc_lower[i] = d->glb_lower[i] + dx *  d->G_strt[i];
      d->loc_upper[i] = d->glb_upper[i] + dx * (d->G_strt[i] + thisdm_size);
      d->L_ntot[i] = d->L_nint[i] + 2 * d->n_ghst;
      d->L_strt[i] = d->n_ghst;
    }
    switch (d->n_dims) {
    case 1: _domain_maketags1d(d); break;
    case 2: _domain_maketags2d(d); break;
    case 3: _domain_maketags3d(d); break;
    }
    printf("[cow] subgrid layout is (%d %d %d)\n",
           d->proc_sizes[0], d->proc_sizes[1], d->proc_sizes[2]);
  }
  else {
#endif
    for (int i=0; i<d->n_dims; ++i) {
      d->L_nint[i] = d->G_ntot[i];
      d->L_ntot[i] = d->G_ntot[i] + 2 * d->n_ghst;
      d->L_strt[i] = d->n_ghst;
      d->G_strt[i] = 0;
      d->loc_lower[i] = d->glb_lower[i];
      d->loc_upper[i] = d->glb_upper[i];
    }
#if (COW_MPI)
  }
#endif
#if (COW_HDF5)
  _io_domain_commit(d);
#endif
  d->committed = 1;
}
int cow_domain_getnumlocalzonesincguard(cow_domain *d, int dim)
{
  switch (dim) {
  case COW_ALL_DIMS: return d->L_ntot[0] * d->L_ntot[1] * d->L_ntot[2];
  case 0: return d->L_ntot[0];
  case 1: return d->L_ntot[1];
  case 2: return d->L_ntot[2];
  default: return 0;
  }
}
int cow_domain_getnumlocalzonesinterior(cow_domain *d, int dim)
{
  switch (dim) {
  case COW_ALL_DIMS: return d->L_nint[0] * d->L_nint[1] * d->L_nint[2];
  case 0: return d->L_nint[0];
  case 1: return d->L_nint[1];
  case 2: return d->L_nint[2];
  default: return 0;
  }
}
int cow_domain_getnumglobalzones(cow_domain *d, int dim)
{
  switch (dim) {
  case COW_ALL_DIMS: return d->G_ntot[0] * d->G_ntot[1] * d->G_ntot[2];
  case 0: return d->G_ntot[0];
  case 1: return d->G_ntot[1];
  case 2: return d->G_ntot[2];
  default: return 0;
  }
}
int cow_domain_getglobalstartindex(cow_domain *d, int dim)
{
  switch (dim) {
  case 0: return d->G_strt[0];
  case 1: return d->G_strt[1];
  case 2: return d->G_strt[2];
  default: return 0;
  }
}
int cow_domain_getgridspacing(cow_domain *d, int dim)
{
  switch (dim) {
  case 0: return d->dx[0];
  case 1: return d->dx[1];
  case 2: return d->dx[2];
  default: return 0;
  }
}
int cow_domain_getcartrank(cow_domain *d)
{
#if (COW_MPI)
  return d->cart_rank;
#else
  return 0;
#endif
}
int cow_domain_subgridatposition(cow_domain *d, double *x)
{
#if (COW_MPI)
  int index[3];
  double *x0 = d->glb_lower;
  double *x1 = d->glb_upper;
  for (int i=0; i<d->n_dims; ++i) {
    index[i] = d->proc_sizes[i] * (x[i] - x0[i]) / (x1[i] - x0[i]);
  }
  int their_rank;
  MPI_Cart_rank(d->mpi_cart, index, &their_rank);
  return their_rank;
#else
  return 0;
#endif
}
int cow_domain_indexatposition(cow_domain *d, int dim, double x)
// -----------------------------------------------------------------------------
// dim: 0,1,2 for x,y,z
//
// x is a global position, i.e. not relative to this subgrid
// The return value is the integer index, which is relative to this subgrid
//
// Within the zone i+ng, the value (x-x0)/dx ranges from i to i+1.
// Then we correct for ghosts by adding ng.
// -----------------------------------------------------------------------------
{
  if (dim >= 3 || !d->committed) return 0.0;
  return d->n_ghst + (int)((x - d->loc_lower[dim]) / d->dx[dim]);
}
double cow_domain_positionatindex(cow_domain *d, int dim, int index)
{
  if (dim >= 3 || !d->committed) return 0.0;
  return d->loc_lower[dim] + d->dx[dim] * (index - d->n_ghst + 0.5);
}

// -----------------------------------------------------------------------------
//
// cow_dfield interface functions
//
// -----------------------------------------------------------------------------
cow_dfield *cow_dfield_new(cow_domain *domain, const char *name)
{
  cow_dfield *f = (cow_dfield*) malloc(sizeof(cow_dfield));
  cow_dfield field = {
    .name = NULL,
    .members = NULL,
    .n_members = 0,
    .member_iter = 0,
    .data = NULL,
    .stride = { 0, 0, 0 },
    .committed = 0,
    .ownsdata = 0,
    .domain = domain,
    .transform = NULL,
    .samplecoords = NULL,
    .sampleresult = NULL,
    .samplecoordslen = 0,
    .samplemode = COW_SAMPLE_LINEAR,
  } ;
  *f = field;
  cow_dfield_setname(f, name);
  return f;
}
void cow_dfield_del(cow_dfield *f)
{
#if (COW_MPI)
  _dfield_freetype(f);
#endif
  for (int n=0; n<f->n_members; ++n) free(f->members[n]);
  free(f->members);
  free(f->name);
  if (f->ownsdata) {
    free(f->data);
  }
  free(f->samplecoords);
  free(f->sampleresult);
  free(f);
}
cow_dfield *cow_dfield_dup(cow_dfield *f)
{
  cow_dfield *g = cow_dfield_new(f->domain, f->name);
  for (int n=0; n<f->n_members; ++n) {
    cow_dfield_addmember(g, f->members[n]);
  }
  cow_dfield_commit(g);
  memcpy(g->data, f->data, cow_dfield_getdatabytes(f));
  g->member_iter = f->member_iter;
  g->transform = f->transform;
  return g;
}
void cow_dfield_setname(cow_dfield *f, const char *name)
{
  f->name = (char*) realloc(f->name, strlen(name)+1);
  strcpy(f->name, name);
}
int cow_dfield_getstride(cow_dfield *f, int dim)
{
  if (dim >= 3 || !f->committed) return 0;
  return f->stride[dim];
}
int cow_dfield_getnmembers(cow_dfield *f)
{
  return f->n_members;
}
const char *cow_dfield_getname(cow_dfield *f)
{
  return f->name;
}
cow_domain *cow_dfield_getdomain(cow_dfield *f)
{
  return f->domain;
}
size_t cow_dfield_getdatabytes(cow_dfield *f)
{
  if (!f->committed) return 0;
  return cow_domain_getnumlocalzonesincguard(f->domain, COW_ALL_DIMS) *
    f->n_members * sizeof(double);
}
void cow_dfield_setbuffer(cow_dfield *f, void *buffer)
// -----------------------------------------------------------------------------
//
// -> If `f` already owns its data:
// --> (A) If buffer is NULL or (not NULL and equal to f->data), then do nothing
// --> (B) Otherwise, `f` frees and disowns its data, re-pointing it to `buffer`
//
// -> If `f` foes not own its data:
// --> (C) If buffer is NULL, then `f` reallocates and adopts its buffer
// --> (D) Otherwise, `f` re-points its buffer to `buffer`
//
// -----------------------------------------------------------------------------
{
  if (f->ownsdata) {
    if (buffer == NULL || (buffer != NULL && buffer == f->data)) {
      // (A)
      // do nothing
    }
    else {
      // (B)
      free(f->data);
      f->data = buffer;
      f->ownsdata = 0;
    }
  }
  else {
    if (buffer == NULL) {
      // (C)
      int nz = cow_domain_getnumlocalzonesincguard(f->domain, COW_ALL_DIMS);
      f->data = malloc(nz * f->n_members * sizeof(double));
      f->ownsdata = 1;
    }
    else {
      // (D)
      f->data = buffer;
    }
  }
}
int cow_dfield_getownsdata(cow_dfield *f)
{
  return f->ownsdata;
}
void *cow_dfield_getbuffer(cow_dfield *f)
{
  return f->data;
}
void cow_dfield_addmember(cow_dfield *f, const char *name)
{
  if (f->committed) return;
  f->n_members++;
  f->members = (char**) realloc(f->members, f->n_members*sizeof(char*));
  f->members[f->n_members-1] = (char*) malloc(strlen(name)+1);
  strcpy(f->members[f->n_members-1], name);
}
const char *cow_dfield_iteratemembers(cow_dfield *f)
{
  f->member_iter = 0;
  return cow_dfield_nextmember(f);
}
const char *cow_dfield_nextmember(cow_dfield *f)
{
  return f->member_iter++ < f->n_members ? f->members[f->member_iter-1] : NULL;
}
void cow_dfield_commit(cow_dfield *f)
{
  if (f->committed) return;
#if (COW_MPI)
  if (cow_mpirunning()) {
    switch (f->domain->n_dims) {
    case 1: _dfield_maketype1d(f); break;
    case 2: _dfield_maketype2d(f); break;
    case 3: _dfield_maketype3d(f); break;
    }
  }
#endif
  // ---------------------------------------------------------------------------
  // The way cow_dfield is initialized, f does not own its buffer and its value
  // is NULL. This corresponds to case (C) in setbuffer: `f` takes ownership of
  // and allocates its buffer. If before being committed, client code has called
  // set_buffer with something other than NULL, then f->ownsdata is still false,
  // but f->data is not NULL. Then the call below triggers case (D) which will
  // have no effect. If client code has called set_buffer with NULL prior to
  // this call, then case (C) will already have been realized, `f` will already
  // own its data, and all subsequent redundant calls including the one below
  // trigger case (A) and have no effect.
  // ---------------------------------------------------------------------------
  cow_dfield_setbuffer(f, f->data);
  int *N = f->domain->L_ntot;
  switch (f->domain->n_dims) {
  case 1:
    f->stride[0] = f->n_members;
    f->stride[1] = 0;
    f->stride[2] = 0;
    break;
  case 2:
    f->stride[0] = f->n_members * N[1];
    f->stride[1] = f->n_members;
    f->stride[2] = 0;
    break;
  case 3:
    f->stride[0] = f->n_members * N[2] * N[1];
    f->stride[1] = f->n_members * N[2];
    f->stride[2] = f->n_members;
    break;
  }
  f->committed = 1;
}
void cow_dfield_syncguard(cow_dfield *f)
{
  if (f->domain->n_ghst == 0) return;
  if (cow_mpirunning()) {
#if (COW_MPI)
    cow_domain *d = f->domain;
    int N = d->num_neighbors;
    MPI_Request *requests = (MPI_Request*) malloc(2*N*sizeof(MPI_Request));
    MPI_Status *statuses = (MPI_Status*) malloc(2*N*sizeof(MPI_Status));
    for (int n=0; n<N; ++n) {
      MPI_Request req1, req2;
      int st = d->send_tags[n];
      int rt = d->recv_tags[n];
      int nr = d->neighbors[n];
      MPI_Isend(f->data, 1, f->send_type[n], nr, st, d->mpi_cart, &req1);
      MPI_Irecv(f->data, 1, f->recv_type[n], nr, rt, d->mpi_cart, &req2);
      requests[2*n+0] = req1;
      requests[2*n+1] = req2;
    }
    MPI_Waitall(2*N, requests, statuses);
    free(requests);
    free(statuses);
#endif
  }
  else {
    double *data = (double*) f->data;
    int *nint = f->domain->L_nint;
    int *ntot = f->domain->L_ntot;
    int *s = f->stride;
    int nq = f->n_members;
    int ng = f->domain->n_ghst;
    switch (f->domain->n_dims) {
    case 1:
      for (int i=0; i<ntot[0]; ++i) {
        int m0 = i*s[0];
        int m1 = i*s[0];
        if (i <            ng) m0 += nint[0] * s[0];
        if (i >= nint[0] + ng) m0 -= nint[0] * s[0];
        if (m0 != m1) memcpy(data + m1, data + m0, nq * sizeof(double));
      }
      break;
    case 2:
      for (int i=0; i<ntot[0]; ++i) {
        for (int j=0; j<ntot[1]; ++j) {
          int m0 = i*s[0] + j*s[1];
          int m1 = i*s[0] + j*s[1];
          if (i <            ng) m0 += nint[0] * s[0];
          if (i >= nint[0] + ng) m0 -= nint[0] * s[0];
          if (j <            ng) m0 += nint[1] * s[1];
          if (j >= nint[1] + ng) m0 -= nint[1] * s[1];
          if (m0 != m1) memcpy(data + m1, data + m0, nq * sizeof(double));
        }
      }
      break;
    case 3:
      for (int i=0; i<ntot[0]; ++i) {
        for (int j=0; j<ntot[1]; ++j) {
          for (int k=0; k<ntot[2]; ++k) {
            int m0 = i*s[0] + j*s[1] + k*s[2];
            int m1 = i*s[0] + j*s[1] + k*s[2];
            if (i <            ng) m0 += nint[0] * s[0];
            if (i >= nint[0] + ng) m0 -= nint[0] * s[0];
            if (j <            ng) m0 += nint[1] * s[1];
            if (j >= nint[1] + ng) m0 -= nint[1] * s[1];
            if (k <            ng) m0 += nint[2] * s[2];
            if (k >= nint[2] + ng) m0 -= nint[2] * s[2];
            if (m0 != m1) memcpy(data + m1, data + m0, nq * sizeof(double));
          }
        }
      }
      break;
    }
  }
}

void cow_dfield_extract(cow_dfield *f, const int *I0, const int *I1, void *out)
{
  _dfield_extractreplace(f, I0, I1, out, 'e');
}
void cow_dfield_replace(cow_dfield *f, const int *I0, const int *I1, void *out)
{
  _dfield_extractreplace(f, I0, I1, out, 'r');
}
void _dfield_extractreplace(cow_dfield *f, const int *I0, const int *I1,
                            void *out, char op)
{
  int mi = I1[0] - I0[0];
  int mj = I1[1] - I0[1];
  int mk = I1[2] - I0[2];
  int si = cow_dfield_getstride(f, 0);
  int sj = cow_dfield_getstride(f, 1);
  int sk = cow_dfield_getstride(f, 2);
  size_t sz = sizeof(double);
  double *dst = (double*) f->data;
  double *src = (double*) out;
  switch (f->domain->n_dims) {
  case 1: {
    int ti = f->n_members;
    for (int i=0; i<mi; ++i) {
      int m0 = (i+I0[0])*si;
      int m1 = i*ti;
      if (op == 'e') {
        memcpy(src + m1, dst + m0, f->n_members * sz);
      }
      else if (op == 'r') {
        memcpy(dst + m0, src + m1, f->n_members * sz);
      }
    }
  } break;
  case 2: {
    int ti = f->n_members * mj;
    int tj = f->n_members;
    for (int i=0; i<mi; ++i) {
      for (int j=0; j<mj; ++j) {
        int m0 = (i+I0[0])*si + (j+I0[1])*sj;
        int m1 = i*ti + j*tj;
        if (op == 'e') {
          memcpy(src + m1, dst + m0, f->n_members * sz);
        }
        else if (op == 'r') {
          memcpy(dst + m0, src + m1, f->n_members * sz);
        }
      }
    }
  } break;
  case 3: {
    int ti = f->n_members * mk * mj;
    int tj = f->n_members * mk;
    int tk = f->n_members;
    for (int i=0; i<mi; ++i) {
      for (int j=0; j<mj; ++j) {
        for (int k=0; k<mk; ++k) {
          int m0 = (i+I0[0])*si + (j+I0[1])*sj + (k+I0[2])*sk;
          int m1 = i*ti + j*tj + k*tk;
          if (op == 'e') {
            memcpy(src + m1, dst + m0, f->n_members * sz);
          }
          else if (op == 'r') {
            memcpy(dst + m0, src + m1, f->n_members * sz);
          }
        }
      }
    }
  } break;
  }
}
static void _reduce(double *result, double **args, int **strides, void *udata)
{
  void **u = (void**)udata;
  cow_dfield *f = (cow_dfield*) u[0];
  double *min = &((double*)u[1])[0];
  double *max = &((double*)u[1])[1];
  double *sum = &((double*)u[1])[2];
  double y;
  f->transform(&y, args, strides, NULL);
  if (y > *max) *max = y;
  if (y < *min) *min = y;
  *sum += y;
}
void cow_dfield_reduce(cow_dfield *f, cow_transform op, double *result)
{
  void *udata[2] = { f, result };
  result[0] = 1e10; // min
  result[1] =-1e10; // max
  result[2] = 0.0; // sum
  f->transform = op;
  cow_dfield_loop(f, _reduce, udata);
  if (!cow_mpirunning()) return;
#if (COW_MPI)
  cow_domain *d = f->domain;
  MPI_Allreduce(MPI_IN_PLACE, &result[0], 1, MPI_DOUBLE, MPI_MIN, d->mpi_cart);
  MPI_Allreduce(MPI_IN_PLACE, &result[1], 1, MPI_DOUBLE, MPI_MAX, d->mpi_cart);
  MPI_Allreduce(MPI_IN_PLACE, &result[2], 1, MPI_DOUBLE, MPI_SUM, d->mpi_cart);
#endif
}
void cow_dfield_loop(cow_dfield *f, cow_transform op, void *udata)
{
  int *S = f->stride;
  int ni = cow_domain_getnumlocalzonesinterior(f->domain, 0);
  int nj = cow_domain_getnumlocalzonesinterior(f->domain, 1);
  int nk = cow_domain_getnumlocalzonesinterior(f->domain, 2);
  int ng = cow_domain_getguard(f->domain);
  switch (f->domain->n_dims) {
  case 1:
    for (int i=ng; i<ni+ng; ++i) {
      double *x = (double*)f->data + (S[0]*i);
      op(NULL, &x, &S, udata);
    }
    break;
  case 2:
    for (int i=ng; i<ni+ng; ++i) {
      for (int j=ng; j<nj+ng; ++j) {
        double *x = (double*)f->data + (S[0]*i + S[1]*j);
        op(NULL, &x, &S, udata);
      }
    }
    break;
  case 3:
    for (int i=ng; i<ni+ng; ++i) {
      for (int j=ng; j<nj+ng; ++j) {
        for (int k=ng; k<nk+ng; ++k) {
          double *x = (double*)f->data + (S[0]*i + S[1]*j + S[2]*k);
          op(NULL, &x, &S, udata);
        }
      }
    }
    break;
  }
}
void cow_dfield_transform(cow_dfield *result, cow_dfield **args, int nargs,
                          cow_transform op, void *udata)
{
  int ni = cow_domain_getnumlocalzonesinterior(result->domain, 0);
  int nj = cow_domain_getnumlocalzonesinterior(result->domain, 1);
  int nk = cow_domain_getnumlocalzonesinterior(result->domain, 2);
  int ng = cow_domain_getguard(result->domain);
  int *rs = result->stride;
  int **S = (int**) malloc(nargs * sizeof(int*));
  double **x = (double**) malloc(nargs * sizeof(double*));
  for (int n=0; n<nargs; ++n) {
    S[n] = args[n]->stride;
  }
  switch (result->domain->n_dims) {
  case 1:
    for (int i=ng; i<ni+ng; ++i) {
      for (int n=0; n<nargs; ++n) {
        x[n] = (double*)args[n]->data + (S[n][0]*i);
      }
      int m1 = rs[0]*i;
      op((double*)result->data + m1, x, S, udata);
    }
    break;
  case 2:
    for (int i=ng; i<ni+ng; ++i) {
      for (int j=ng; j<nj+ng; ++j) {
        for (int n=0; n<nargs; ++n) {
          x[n] = (double*)args[n]->data + (S[n][0]*i + S[n][1]*j);
        }
        int m1 = rs[0]*i + rs[1]*j;
        op((double*)result->data + m1, x, S, udata);
      }
    }
    break;
  case 3:
    for (int i=ng; i<ni+ng; ++i) {
      for (int j=ng; j<nj+ng; ++j) {
        for (int k=ng; k<nk+ng; ++k) {
          for (int n=0; n<nargs; ++n) {
            x[n] = (double*)args[n]->data + (S[n][0]*i + S[n][1]*j + S[n][2]*k);
          }
          int m1 = rs[0]*i + rs[1]*j + rs[2]*k;
          op((double*)result->data + m1, x, S, udata);
        }
      }
    }
    break;
  }
  free(S);
  free(x);
  cow_dfield_syncguard(result);
}

#if (COW_MPI)
void _domain_maketags1d(cow_domain *d)
{
  d->num_neighbors = 3-1;
  _domain_alloctags(d);
  int n = 0;
  for (int i=-1; i<=1; ++i) {
    if (i == 0) continue; // don't include self
    int rel_index [] = { i };
    int index[] = { d->proc_index[0] + rel_index[0] };
    int their_rank;
    MPI_Cart_rank(d->mpi_cart, index, &their_rank);
    d->neighbors[n] = their_rank;
    d->send_tags[n] = 1*(+i+5);
    d->recv_tags[n] = 1*(-i+5);
    ++n;
  }
}
void _domain_maketags2d(cow_domain *d)
{
  d->num_neighbors = 9-1;
  _domain_alloctags(d);
  int n = 0;
  for (int i=-1; i<=1; ++i) {
    for (int j=-1; j<=1; ++j) {
      if (i == 0 && j == 0) continue; // don't include self
      int rel_index [] = { i, j };
      int index[] = { d->proc_index[0] + rel_index[0],
                      d->proc_index[1] + rel_index[1] };
      int their_rank;
      MPI_Cart_rank(d->mpi_cart, index, &their_rank);
      d->neighbors[n] = their_rank;
      d->send_tags[n] = 10*(+i+5) + 1*(+j+5);
      d->recv_tags[n] = 10*(-i+5) + 1*(-j+5);
      ++n;
    }
  }
}
void _domain_maketags3d(cow_domain *d)
{
  d->num_neighbors = 27-1;
  _domain_alloctags(d);
  int n = 0;
  for (int i=-1; i<=1; ++i) {
    for (int j=-1; j<=1; ++j) {
      for (int k=-1; k<=1; ++k) {
        if (i == 0 && j == 0 && k == 0) continue; // don't include self
        int rel_index [] = { i, j, k };
        int index[] = { d->proc_index[0] + rel_index[0],
                        d->proc_index[1] + rel_index[1],
                        d->proc_index[2] + rel_index[2] };
        int their_rank;
        MPI_Cart_rank(d->mpi_cart, index, &their_rank);
        d->neighbors[n] = their_rank;
        d->send_tags[n] = 100*(+i+5) + 10*(+j+5) + 1*(+k+5);
        d->recv_tags[n] = 100*(-i+5) + 10*(-j+5) + 1*(-k+5);
        ++n;
      }
    }
  }
}
void _domain_alloctags(cow_domain *d)
{
  int N = d->num_neighbors;
  d->neighbors = (int*) malloc(N*sizeof(int));
  d->send_tags = (int*) malloc(N*sizeof(int));
  d->recv_tags = (int*) malloc(N*sizeof(int));
}
void _domain_freetags(cow_domain *d)
{
  free(d->neighbors);
  free(d->send_tags);
  free(d->recv_tags);
}
void _dfield_maketype1d(cow_dfield *f)
{
  _dfield_alloctype(f);
  cow_domain *d = f->domain;
  int ng = d->n_ghst;
  int c = MPI_ORDER_C;
  int n = 0;
  if (d->n_ghst == 0) return;
  for (int i=-1; i<=1; ++i) {
    if (i == 0) continue;  // don't include self
    int Plx[] = { ng, ng, d->L_nint[0] };
    int Qlx[] = {  0, ng, d->L_nint[0] + ng };
    int start_send[] = { Plx[i+1] };
    int start_recv[] = { Qlx[i+1] };
    int sub[] = { (1-abs(i))*d->L_nint[0] + abs(i)*ng };
    MPI_Datatype send, recv, type;
    MPI_Type_contiguous(f->n_members, MPI_DOUBLE, &type);
    MPI_Type_create_subarray(1, d->L_ntot, sub, start_send, c, type, &send);
    MPI_Type_create_subarray(1, d->L_ntot, sub, start_recv, c, type, &recv);
    MPI_Type_commit(&send);
    MPI_Type_commit(&recv);
    MPI_Type_free(&type);
    f->send_type[n] = send;
    f->recv_type[n] = recv;
    ++n;
  }
}
void _dfield_maketype2d(cow_dfield *f)
{
  _dfield_alloctype(f);
  cow_domain *d = f->domain;
  int ng = d->n_ghst;
  int c = MPI_ORDER_C;
  int n = 0;
  if (d->n_ghst == 0) return;
  for (int i=-1; i<=1; ++i) {
    for (int j=-1; j<=1; ++j) {
      if (i == 0 && j == 0) continue; // don't include self
      int Plx[] = { ng, ng, d->L_nint[0] };
      int Ply[] = { ng, ng, d->L_nint[1] };
      int Qlx[] = {  0, ng, d->L_nint[0] + ng };
      int Qly[] = {  0, ng, d->L_nint[1] + ng };
      int start_send[] = { Plx[i+1], Ply[j+1] };
      int start_recv[] = { Qlx[i+1], Qly[j+1] };
      int sub[] = { (1-abs(i))*d->L_nint[0] + abs(i)*ng,
                    (1-abs(j))*d->L_nint[1] + abs(j)*ng };
      MPI_Datatype send, recv, type;
      MPI_Type_contiguous(f->n_members, MPI_DOUBLE, &type);
      MPI_Type_create_subarray(2, d->L_ntot, sub, start_send, c, type, &send);
      MPI_Type_create_subarray(2, d->L_ntot, sub, start_recv, c, type, &recv);
      MPI_Type_commit(&send);
      MPI_Type_commit(&recv);
      MPI_Type_free(&type);
      f->send_type[n] = send;
      f->recv_type[n] = recv;
      ++n;
    }
  }
}
void _dfield_maketype3d(cow_dfield *f)
{
  _dfield_alloctype(f);
  cow_domain *d = f->domain;
  int ng = d->n_ghst;
  int c = MPI_ORDER_C;
  int n = 0;
  if (d->n_ghst == 0) return;
  for (int i=-1; i<=1; ++i) {
    for (int j=-1; j<=1; ++j) {
      for (int k=-1; k<=1; ++k) {
        if (i == 0 && j == 0 && k == 0) continue; // don't include self
        int Plx[] = { ng, ng, d->L_nint[0] };
        int Ply[] = { ng, ng, d->L_nint[1] };
        int Plz[] = { ng, ng, d->L_nint[2] };
        int Qlx[] = {  0, ng, d->L_nint[0] + ng };
        int Qly[] = {  0, ng, d->L_nint[1] + ng };
        int Qlz[] = {  0, ng, d->L_nint[2] + ng };
        int start_send[] = { Plx[i+1], Ply[j+1], Plz[k+1] };
        int start_recv[] = { Qlx[i+1], Qly[j+1], Qlz[k+1] };
        int sub[] = { (1-abs(i))*d->L_nint[0] + abs(i)*ng,
                      (1-abs(j))*d->L_nint[1] + abs(j)*ng,
                      (1-abs(k))*d->L_nint[2] + abs(k)*ng };
        MPI_Datatype send, recv, type;
        MPI_Type_contiguous(f->n_members, MPI_DOUBLE, &type);
        MPI_Type_create_subarray(3, d->L_ntot, sub, start_send, c, type, &send);
        MPI_Type_create_subarray(3, d->L_ntot, sub, start_recv, c, type, &recv);
        MPI_Type_commit(&send);
        MPI_Type_commit(&recv);
        MPI_Type_free(&type);
        f->send_type[n] = send;
        f->recv_type[n] = recv;
        ++n;
      }
    }
  }
}


void _dfield_alloctype(cow_dfield *f)
{
  if (f->domain->n_ghst == 0) return;
  cow_domain *d = f->domain;
  int N = d->num_neighbors;
  f->send_type = (MPI_Datatype*) malloc(N * sizeof(MPI_Datatype));
  f->recv_type = (MPI_Datatype*) malloc(N * sizeof(MPI_Datatype));
}
void _dfield_freetype(cow_dfield *f)
{
  if (f->domain->n_ghst == 0) return;
  cow_domain *d = f->domain;
  int N = d->num_neighbors;
  for (int n=0; n<N; ++n) MPI_Type_free(&f->send_type[n]);
  for (int n=0; n<N; ++n) MPI_Type_free(&f->recv_type[n]);
  free(f->send_type);
  free(f->recv_type);
}
#endif

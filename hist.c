
#include <string.h>
#include <math.h>
#define COW_PRIVATE_DEFS
#include "cow.h"
#define MODULE "hist"


static int H5Lexists_safe(hid_t base, const char *path);

cow_histogram *cow_histogram_new()
{
  cow_histogram *h = (cow_histogram*) malloc(sizeof(cow_histogram));
  cow_histogram hist = {
    .nbinsx = 1,
    .nbinsy = 1,
    .x0 = 0.0,
    .x1 = 1.0,
    .y0 = 0.0,
    .y1 = 1.0,
    .bedgesx = NULL,
    .bedgesy = NULL,
    .weight = NULL,
    .counts = NULL,
    .nickname = NULL,
    .fullname = NULL,
    .binmode = COW_HIST_BINMODE_AVERAGE,
    .spacing = COW_HIST_SPACING_LINEAR,
    .n_dims = 0,
    .committed = 0,
    .transform = NULL,
#if (COW_MPI)
    .comm = MPI_COMM_WORLD,
#endif
  } ;
  *h = hist;
  cow_histogram_setnickname(h, "histogram");
  return h;
}

void cow_histogram_commit(cow_histogram *h)
{
  h->n_dims = h->nbinsy > 1 ? 2 : 1;
  if (h->n_dims == 1) {
    const double dx = (h->x1 - h->x0) / h->nbinsx;
    h->bedgesx = (double*) malloc((h->nbinsx+1)*sizeof(double));
    h->weight = (double*) malloc((h->nbinsx)*sizeof(double));
    h->counts = (long*) malloc((h->nbinsx)*sizeof(long));
    for (int n=0; n<h->nbinsx+1; ++n) {
      if (h->spacing == COW_HIST_SPACING_LOG) {
        h->bedgesx[n] = h->x0 * pow(h->x1 / h->x0, (double)n / h->nbinsx);
      }
      else if (h->spacing == COW_HIST_SPACING_LINEAR) {
        h->bedgesx[n] = h->x0 + n * dx;
      }
    }
    for (int n=0; n<h->nbinsx; ++n) {
      h->counts[n] = 0;
      h->weight[n] = 0.0;
    }
  }
  else if (h->n_dims == 2) {
    const int nbins = h->nbinsx * h->nbinsy;
    const double dx = (y1-y0) / h->nbinsx;
    const double dy = (y1-y0) / h->nbinsy;
    h->bedgesx = (double*) malloc((h->nbinsx+1)*sizeof(double));
    h->bedgesy = (double*) malloc((h->nbinsy+1)*sizeof(double));
    h->weight = (double*) malloc(nbins*sizeof(double));
    h->counts = (long*) malloc(nbins*sizeof(long));
    for (int n=0; n<h->nbinsx+1; ++n) {
      if (h->spacing == COW_HIST_SPACING_LOG) {
        h->bedgesx[n] = h->x0 * pow(h->x1/h->x0, (double)n / h->nbinsx);
      }
      else if (h->spacing == COW_HIST_SPACING_LINEAR) {
        h->bedgesx[n] = h->x0 + n * dx;
      }
    }
    for (int n=0; n<h->nbinsy+1; ++n) {
      if (h->spacing == COW_HIST_SPACING_LOG) {
        h->bedgesy[n] = h->y0 * pow(h->y1/h->y0, (double)n / h->nbinsy);
      }
      else if (h->spacing == COW_HIST_SPACING_LINEAR) {
        h->bedgesy[n] = h->y0 + n * dy;
      }
    }
    for (int n=0; n<nbins; ++n) {
      h->counts[n] = 0;
      h->weight[n] = 0.0;
    }
  }
#if (COW_MPI)
  MPI_Comm_dup(h->comm, &h->comm);
#endif
  h->committed = 1;
}
void cow_histogram_del(cow_histogram *h)
{
#if (COW_MPI)
  MPI_Comm_free(&h->comm);
#endif
  free(h->bedgesx);
  free(h->bedgesy);
  free(h->weight);
  free(h->counts);
  free(h->nickname);
  free(h->fullname);
  free(h);
}
void cow_histogram_setdomaincomm(cow_histogram *h, cow_domain *d)
{
  if (h->committed) return;
  h->comm = d->mpi_cart;
}
void cow_histogram_setbinmode(cow_histogram *h, int binmode)
{
  if (h->committed) return;
  switch (binmode) {
  case COW_HIST_BINMODE_DENSITY: h->binmode = binmode; break;
  case COW_HIST_BINMODE_AVERAGE: h->binmode = binmode; break;
  case COW_HIST_BINMODE_COUNTS: h->binmode = binmode; break;
  default: printf("[%s] error: no such bin mode\n", MODULE); break;
  }
}
void cow_histogram_setspacing(cow_histogram *h, int spacing)
{
  if (h->committed) return;
  switch (spacing) {
  case COW_HIST_SPACING_LINEAR: h->spacing = spacing; break;
  case COW_HIST_SPACING_LOG: h->spacing = spacing; break;
  default: printf("[%s] error: no such spacing\n", MODULE); break;
  }
}
void cow_histogram_setnbins(cow_histogram *h, int dim, int nbins)
{
  if (h->committed) return;
  switch (dim) {
  case 0: h->nbinsx = nbins; break;
  case 1: h->nbinsy = nbins; break;
  case COW_ALL_DIMS: h->nbinsx = h->nbinsy = nbins; break;
  default: break;
  }
}
void cow_histogram_setlower(cow_histogram *h, int dim, double v0)
{
  if (h->committed) return;
  switch (dim) {
  case 0: h->x0 = v0; break;
  case 1: h->y0 = v0; break;
  case COW_ALL_DIMS: h->x0 = h->y0 = v0; break;
  default: break;
  }
}
void cow_histogram_setupper(cow_histogram *h, int dim, double v1)
{
  if (h->committed) return;
  switch (dim) {
  case 0: h->x1 = v1; break;
  case 1: h->y1 = v1; break;
  case COW_ALL_DIMS: h->x1 = h->y1 = v1; break;
  default: break;
  }
}
void cow_histogram_setfullname(cow_histogram *h, const char *fullname)
{
  h->fullname = (char*) realloc(h->fullname, strlen(fullname)+1);
  strcpy(h->fullname, fullname);
}
void cow_histogram_setnickname(cow_histogram *h, const char *nickname)
{
  h->nickname = (char*) realloc(h->nickname, strlen(nickname)+1);
  strcpy(h->nickname, nickname);
}
static void popcb(double *result, double **args, int **s, void *u)
{
  cow_histogram *h = (cow_histogram*) u;
  double y[2];
  h->transform(y, args, s, u);
  if (h->n_dims == 1) {
    cow_histogram_addsample1(h, y[0], 1.0);
  }
  else if (h->n_dims == 2) {
    cow_histogram_addsample2(h, y[0], y[1], 1.0);
  }
}
void cow_histogram_populate(cow_histogram *h, cow_dfield *f, cow_transform op)
{
  cow_domain *d = f->domain;
  h->transform = op;
  cow_histogram_setdomaincomm(h, d);
  cow_dfield_loop(f, popcb, h);
}
void cow_histogram_addsample1(cow_histogram *h, double x, double w)
{
  if (!h->committed) return;
  for (int n=0; n<h->nbinsx; ++n) {
    if (h->bedgesx[n] < x && x < h->bedgesx[n+1]) {
      h->weight[n] += w;
      h->counts[n] += 1;
      return;
    }
  }
}
void cow_histogram_addsample2(cow_histogram *h, double x, double y, double w)
{
  if (!h->committed) return;
  int nx=-1, ny=-1;
  for (int n=0; n<h->nbinsx; ++n) {
    if (h->bedgesx[n] < x && x < h->bedgesx[n+1]) {
      nx = n;
      break;
    }
  }
  for (int n=0; n<h->nbinsy; ++n) {
    if (h->bedgesy[n] < y && y < h->bedgesy[n+1]) {
      ny = n;
      break;
    }
  }
  if (nx == -1 || ny == -1) {
    return;
  }
  else {
    h->counts[nx * h->nbinsy + ny] += 1;
    h->weight[nx * h->nbinsy + ny] += w;
    return;
  }
}
void cow_histogram_synchronize(cow_histogram *h)
{
  if (!h->committed) return;
#if (COW_MPI)
  int run_uses_mpi;
  MPI_Initialized(&run_uses_mpi);
  if (!run_uses_mpi) {
    return;
  }
  int nbins = h->nbinsx * h->nbinsy;
  MPI_Comm c = h->comm;
  MPI_Allreduce(MPI_IN_PLACE, h->weight, nbins, MPI_DOUBLE, MPI_SUM, c);
  MPI_Allreduce(MPI_IN_PLACE, h->counts, nbins, MPI_LONG, MPI_SUM, c);
#endif
}

double cow_histogram_getbinval(cow_histogram *h, int i, int j)
{
  if (!h->committed) return 0.0;
  if (i > h->nbinsx || j > h->nbinsy) return 0.0;
  int c = h->counts[i*h->nbinsy + j];
  double w = h->weight[i*h->nbinsy + j];
  double dx = h->n_dims >= 1 ? h->bedgesx[i+1] - h->bedgesx[i] : 1.0;
  double dy = h->n_dims >= 2 ? h->bedgesy[j+1] - h->bedgesy[j] : 1.0;
  switch (h->binmode) {
  case COW_HIST_BINMODE_AVERAGE:
    return c == 0 ? 0.0 : w / c;
  case COW_HIST_BINMODE_DENSITY:
    return w / (dx*dy);
  case COW_HIST_BINMODE_COUNTS:
    return w;
  default:
    return 0.0;
  }
}

void cow_histogram_dumpascii(cow_histogram *h, const char *fn)
// -----------------------------------------------------------------------------
// Dumps the histogram as ascii to the file named `fn`. Synchronizes it across
// processes before doing so. All MPI processes must participate, the function
// uses rank 0 to do the write.
// -----------------------------------------------------------------------------
{
  if (!h->committed) return;
  cow_histogram_synchronize(h);
#if (COW_MPI)
  {
    int run_uses_mpi;
    int rank;
    MPI_Initialized(&run_uses_mpi);
    if (run_uses_mpi) {
      MPI_Comm_rank(h->comm, &rank);
      if (rank != 0) {
	return;
      }
    }
  }
#endif
  FILE *file = fopen(fn, "w");
  if (file == NULL) {
    printf("[%s] could not open file %s\n", __FILE__, fn);
    return;
  }
  else {
    printf("[%s] writing histogram as ASCII table to %s\n", MODULE, fn);
  }
  if (h->n_dims == 1) {
    for (int n=0; n<h->nbinsx; ++n) {
      fprintf(file, "%f %f\n", 0.5*(h->bedgesx[n] + h->bedgesx[n+1]),
	      cow_histogram_getbinval(h, n, 0));
    }
  }
  else if (h->n_dims == 2) {
    for (int nx=0; nx<h->nbinsx; ++nx) {
      for (int ny=0; ny<h->nbinsy; ++ny) {
	fprintf(file, "%f %f %f\n",
		0.5*(h->bedgesx[nx] + h->bedgesx[nx+1]),
		0.5*(h->bedgesy[ny] + h->bedgesy[ny+1]),
		cow_histogram_getbinval(h, nx, ny));
      }
    }
  }
  fclose(file);
}

void cow_histogram_dumphdf5(cow_histogram *h, const char *fn, const char *gn)
// -----------------------------------------------------------------------------
// Dumps the histogram to the HDF5 file named `fn`, under the group
// `gn`/h->fullname. Synchronizes it across processes before doing so. All MPI
// processes must participate, the function uses rank 0 to do the write.
// -----------------------------------------------------------------------------
{
#if (COW_HDF5)
  if (!h->committed) return;
  char gname[1024];
  int rank = 0;
  snprintf(gname, 1024, "%s/%s", gn, h->nickname);
  cow_histogram_synchronize(h);
#if (COW_MPI)
  int run_uses_mpi;
  MPI_Initialized(&run_uses_mpi);
  if (run_uses_mpi) MPI_Comm_rank(h->comm, &rank);
#endif
  if (rank == 0) {
    // -------------------------------------------------------------------------
    // The write functions assume the file is already created. Have master
    // create the file if it's not there already.
    // -------------------------------------------------------------------------
    FILE *testf = fopen(fn, "r");
    hid_t fid;
    if (testf == NULL) {
      fid = H5Fcreate(fn, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    }
    else {
      fclose(testf);
      fid = H5Fopen(fn, H5F_ACC_RDWR, H5P_DEFAULT);
    }
    if (H5Lexists_safe(fid, gname)) {
      printf("[%s] writing histogram as HDF5 to %s/%s (clobber existing)\n",
	     MODULE, fn, gname);
      H5Gunlink(fid, gname);
    }
    else {
      printf("[%s] writing histogram as HDF5 to %s/%s\n", MODULE, fn, gname);
    }
    hid_t gcpl = H5Pcreate(H5P_LINK_CREATE);
    H5Pset_create_intermediate_group(gcpl, 1);
    hid_t memb = H5Gcreate(fid, gname, gcpl, H5P_DEFAULT, H5P_DEFAULT);
    H5Pclose(gcpl);
    H5Gclose(memb);
    H5Fclose(fid);
  }
  else {
    return;
  }
  // Create a group to represent this histogram, and an attribute to name it
  // ---------------------------------------------------------------------------
  hid_t fid = H5Fopen(fn, H5F_ACC_RDWR, H5P_DEFAULT);
  hid_t grp = H5Gopen(fid, gname, H5P_DEFAULT);
  if (h->fullname != NULL) {
    hid_t aspc = H5Screate(H5S_SCALAR);
    hid_t strn = H5Tcopy(H5T_C_S1);
    H5Tset_size(strn, strlen(h->fullname));
    hid_t attr = H5Acreate(grp, "fullname", strn, aspc, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, strn, h->fullname); // write the full name
    H5Aclose(attr);
    H5Tclose(strn);
    H5Sclose(aspc);
  }
  // Create a temporary array for the bin centers
  // ---------------------------------------------------------------------------
  int nbins = h->nbinsx * h->nbinsy;
  double *binlocX = (double*) malloc(h->nbinsx * sizeof(double));
  double *binlocY = (double*) malloc(h->nbinsy * sizeof(double));
  double *binvalV = (double*) malloc(nbins * sizeof(double));
  if (h->n_dims >= 1) for (int i=0; i<h->nbinsx; ++i) {
    binlocX[i] = 0.5*(h->bedgesx[i] + h->bedgesx[i+1]);
  }
  if (h->n_dims >= 2) for (int j=0; j<h->nbinsy; ++j) {
    binlocY[j] = 0.5*(h->bedgesy[j] + h->bedgesy[j+1]);
  }
  for (int i=0; i<h->nbinsx; ++i) {
    for (int j=0; j<h->nbinsy; ++j) {
      binvalV[i*h->nbinsy + j] = cow_histogram_getbinval(h, i, j);
    }
  }
  // Create the data sets in the group: binloc (bin centers) and binval (values)
  // ---------------------------------------------------------------------------
  hsize_t sizeX[2] = { h->nbinsx };
  hsize_t sizeY[2] = { h->nbinsy };
  hsize_t sizeZ[2] = { h->nbinsx, h->nbinsy };
  hid_t fspcZ = H5Screate_simple(h->n_dims, sizeZ, NULL);
  if (h->n_dims >= 1) {
    hid_t fspcX = H5Screate_simple(1, sizeX, NULL);
    hid_t dsetbinX = H5Dcreate(grp, "binlocX", H5T_NATIVE_DOUBLE, fspcX,
			       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(dsetbinX, H5T_NATIVE_DOUBLE, fspcX, fspcX, H5P_DEFAULT, binlocX);
    H5Dclose(dsetbinX);
    H5Sclose(fspcX);
  }
  if (h->n_dims >= 2) {
    hid_t fspcY = H5Screate_simple(1, sizeY, NULL);
    hid_t dsetbinY = H5Dcreate(grp, "binlocY", H5T_NATIVE_DOUBLE, fspcY,
			       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(dsetbinY, H5T_NATIVE_DOUBLE, fspcY, fspcY, H5P_DEFAULT, binlocX);
    H5Dclose(dsetbinY);
    H5Sclose(fspcY);
  }
  hid_t dsetvalV = H5Dcreate(grp, "binval", H5T_NATIVE_DOUBLE, fspcZ,
			     H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  H5Dwrite(dsetvalV, H5T_NATIVE_DOUBLE, fspcZ, fspcZ, H5P_DEFAULT, binvalV);
  H5Dclose(dsetvalV);
  H5Sclose(fspcZ);
  H5Gclose(grp);
  H5Fclose(fid);
  free(binlocX);
  free(binlocY);
  free(binvalV);
#endif
}


int H5Lexists_safe(hid_t base, const char *path)
// -----------------------------------------------------------------------------
// The HDF5 specification only allows H5Lexists to be called on an immediate
// child of the current object. However, you may wish to see whether a whole
// relative path exists, returning false if any of the intermediate links are
// not present. This function does that.
// http://www.hdfgroup.org/HDF5/doc/RM/RM_H5L.html#Link-Exists
// -----------------------------------------------------------------------------
{
#if (COW_HDF5)
  hid_t last = base, next;
  char *pch;
  char pathc[2048];
  strcpy(pathc, path);
  pch = strtok(pathc, "/");
  while (pch != NULL) {
    int exists = H5Lexists(last, pch, H5P_DEFAULT);
    if (!exists) {
      if (last != base) H5Gclose(last);
      return 0;
    }
    else {
      next = H5Gopen(last, pch, H5P_DEFAULT);
      if (last != base) H5Gclose(last);
      last = next;
    }
    pch = strtok(NULL, "/");
  }
  if (last != base) H5Gclose(last);
  return 1;
#endif
}

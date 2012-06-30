
#include <string.h>
#include <math.h>
#define COW_PRIVATE_DEFS
#include "cow.h"


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
    .binmode = COW_BINNING_LINSPACE,
    .n_dims = 0,
    .committed = 0
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
      if (h->binmode == COW_BINNING_LOGSPACE) {
        h->bedgesx[n] = h->x0 * pow(h->x1 / h->x0, (double)n / h->nbinsx);
      }
      else {
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
      if (h->binmode == COW_BINNING_LOGSPACE) {
        h->bedgesx[n] = h->x0 * pow(h->x1/h->x0, (double)n / h->nbinsx);
      }
      else {
        h->bedgesx[n] = h->x0 + n * dx;
      }
    }
    for (int n=0; n<h->nbinsy+1; ++n) {
      if (h->binmode == COW_BINNING_LOGSPACE) {
        h->bedgesy[n] = h->y0 * pow(h->y1/h->y0, (double)n / h->nbinsy);
      }
      else {
        h->bedgesy[n] = h->y0 + n * dy;
      }
    }
    for (int n=0; n<nbins; ++n) {
      h->counts[n] = 0;
      h->weight[n] = 0.0;
    }
  }
  h->committed = 1;
}
void cow_histogram_del(cow_histogram *h)
{
  free(h->bedgesx);
  free(h->bedgesy);
  free(h->weight);
  free(h->counts);
  free(h->nickname);
  free(h->fullname);
  free(h);
}

void cow_histogram_setbinmode(cow_histogram *h, int binmode)
{
  if (h->committed) return;
  h->binmode = binmode;
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
  MPI_Comm c = MPI_COMM_WORLD;
  MPI_Allreduce(MPI_IN_PLACE, h->weight, nbins, MPI_DOUBLE, MPI_SUM, c);
  MPI_Allreduce(MPI_IN_PLACE, h->counts, nbins, MPI_LONG, MPI_SUM, c);
#endif
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
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
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
  if (h->n_dims == 1) {
    for (int n=0; n<h->nbinsx; ++n) {
      int c = h->counts[n];
      fprintf(file, "%f %f\n", 0.5*(h->bedgesx[n] + h->bedgesx[n+1]),
	      c == 0 ? 0.0 : h->weight[n]/c);
    }
  }
  else if (h->n_dims == 2) {
    for (int nx=0; nx<h->nbinsx; ++nx) {
      for (int ny=0; ny<h->nbinsy; ++ny) {
	int c = h->counts[nx * h->nbinsy + ny];
	fprintf(file, "%f %f %f\n",
		0.5*(h->bedgesx[nx] + h->bedgesx[nx+1]),
		0.5*(h->bedgesy[ny] + h->bedgesy[ny+1]),
		c == 0 ? 0.0 : h->weight[nx * h->nbinsy + ny] / c);
      }
    }
  }
  fclose(file);
}


void cow_histogram_dumphdf5(cow_histogram *h, const char *fn, const char *gn)
{
  if (!h->committed) return;
#if (COW_MPI)
  int run_uses_mpi;
  int rank = 0;
  MPI_Initialized(&run_uses_mpi);
  if (run_uses_mpi) {
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  }
  if (rank == 0) {
#endif
    // -------------------------------------------------------------------------
    // The write functions assume the file is already created. Have master
    // create the file if it's not there already.
    // -------------------------------------------------------------------------
    FILE *testf = fopen(fn, "r");
    hid_t fid;
    char gname[1024];
    sprintf(gname, "%s/%s", gn, h->nickname);
    if (testf == NULL) {
      fid = H5Fcreate(fn, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    }
    else {
      fclose(testf);
      fid = H5Fopen(fn, H5F_ACC_RDWR, H5P_DEFAULT);
    }
    if (H5Lexists(fid, gname, H5P_DEFAULT)) {
      H5Gunlink(fid, gname);
    }
    hid_t memb = H5Gcreate(fid, gname, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Gclose(memb);
    H5Fclose(fid);
#if (COW_MPI)
  }
#endif
  return;
#if (COW_HDF5)
  // Create a group to represent this histogram, and an attribute to name it
  // ---------------------------------------------------------------------------
  hid_t hgrp;// = H5Gcreate(base, h->nickname, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (h->fullname != NULL) {
    hid_t aspc = H5Screate(H5S_SCALAR);
    hid_t strn = H5Tcopy(H5T_C_S1);
    H5Tset_size(strn, strlen(h->fullname));
    hid_t attr = H5Acreate(hgrp, "fullname", strn, aspc, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, strn, h->fullname); // write the full name
    H5Aclose(attr);
    H5Tclose(strn);
    H5Sclose(aspc);
  }
  /*
  // Create a temporary array for the bin centers
  // ---------------------------------------------------------------------------
  double *binlocX = new double[nbinsX];
  double *binlocY = new double[nbinsY];
  double *binval  = new double[nbins];
  for (int i=0; i<nbinsX; ++i) {
    binlocX[i] = 0.5*(bedgesX[i] + bedgesX[i+1]);
  }
  for (int j=0; j<nbinsY; ++j) {
    binlocY[j] = 0.5*(bedgesY[j] + bedgesY[j+1]);
  }
  for (int i=0; i<nbinsX; ++i) {
    for (int j=0; j<nbinsY; ++j) {
      const long c = counts[i*nbinsY + j];

      switch (binning_mode) {
      case Histogram::BinAverage:
	binval[i*nbinsY + j] = (c == 0) ? 0.0 : weight[i*nbinsY + j] / c;
	break;
      case Histogram::BinDensity:
	binval[i*nbinsY + j] = weight[i*nbinsY + j] /
	  ((bedgesX[i+1] - bedgesX[i])*(bedgesY[j+1] - bedgesY[j]));
	break;
      default:
	binval[i*nbinsY + j] = 0.0;
	break;
      }
    }
  }

  // Create the data sets in the group: binloc (bin centers) and binval (values)
  // ---------------------------------------------------------------------------
  hsize_t sizeX[2] = { nbinsX };
  hsize_t sizeY[2] = { nbinsY };
  hsize_t sizeZ[2] = { nbinsX, nbinsY };
  hid_t fspcX = H5Screate_simple(1, sizeX, NULL);
  hid_t fspcY = H5Screate_simple(1, sizeY, NULL);
  hid_t fspcZ = H5Screate_simple(2, sizeZ, NULL);

  hid_t dsetbinX = H5Dcreate(hgrp, "binlocX", H5T_NATIVE_DOUBLE, fspcX,
                             H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  hid_t dsetbinY = H5Dcreate(hgrp, "binlocY", H5T_NATIVE_DOUBLE, fspcY,
                             H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

  hid_t dsetval = H5Dcreate(hgrp, "binval", H5T_NATIVE_DOUBLE, fspcZ,
                            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

  H5Dwrite(dsetbinX, H5T_NATIVE_DOUBLE, fspcX, fspcX, H5P_DEFAULT, binlocX);
  H5Dwrite(dsetbinY, H5T_NATIVE_DOUBLE, fspcY, fspcY, H5P_DEFAULT, binlocY);
  H5Dwrite(dsetval , H5T_NATIVE_DOUBLE, fspcZ, fspcZ, H5P_DEFAULT, binval);

  H5Dclose(dsetval);
  H5Dclose(dsetbinX);
  H5Dclose(dsetbinY);
  H5Sclose(fspcX);
  H5Sclose(fspcY);
  H5Sclose(fspcZ);
  H5Gclose(hgrp);

  delete [] binlocX;
  delete [] binlocY;
  delete [] binval;
  */
#endif
}

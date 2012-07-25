#!/usr/bin/env python

from distutils.core import setup, Extension
#from numpy.distutils.core import setup, Extension
import numpy as np

# Helpful info on linker environment:
# http://bit.ly/Nsvato

config = {
    'COW_HDF5': 0,
    'COW_HDF5_MPI': 0,
    'COW_FFTW': 0,
    'COW_MPI': 0,
    'include_dirs': [ ],
    'library_dirs': [ ],
    'libraries': [ ],
    'extra_compile_args': [ ],
    'extra_link_args': [ ],
    'NPY_INC': np.get_include() }

try:
    import cow_config
    config.update(cow_config.config)
    print "Using system config"
except:
    print "No system config, using default settings"

config['include_dirs'] += ['../include', np.get_include()]
config['library_dirs'] += ['../lib']
config['extra_link_args'] += ['../lib/libcow.a']

def make_ext(name, sources):
    return Extension(
        name,
        extra_compile_args = ['-std=c99'] + config['extra_compile_args'],
        extra_link_args    = config['extra_link_args'],
        define_macros      = [a for a in config.items() if a[0].startswith('COW')],
        include_dirs       = ["../src"] + config['include_dirs'],
        library_dirs       = config['library_dirs'],
        libraries          = config['libraries'],
        sources            = sources)

cowsrc = ["../src/" + c for c in ['cow.c', 'io.c', 'hist.c', 'samp.c', 'fft.c',
                                  'fft_3d.c', 'remap_3d.c', 'pack_3d.c',
                                  'srhdpack.c']]
cow = make_ext('cowpy.capi._ccow', sources=['cow.i'])# + cowsrc)

setup(name        = 'cowpy',
      version     = '0.4',
      author      = "Jonathan Zrake",
      description = """C.O.W.""",
      ext_modules = [cow],
      packages    = ["cowpy", "cowpy.capi"])
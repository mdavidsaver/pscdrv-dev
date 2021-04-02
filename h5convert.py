#!/usr/bin/env python3
"""Specialized converter from PSCUDPFast .dat file(s) to an HDF5 file.

In a (futile?) attempt to speed up conversion, this script
requires that .dat files:

* Contain only messages with ID 12345.
* All messages have the same body length of 912 bytes.
"""

import sys
import struct

import numpy
import h5py

flush_batch = 1024

header = struct.Struct('!2sHIII')
assert header.size==16

dtype = numpy.dtype([
    ('ps', 'u2'),
    ('msgid', 'u2'),
    ('msglen', 'u4'),
    ('sec', 'u4'),
    ('nsec', 'u4'),
    ('seq', 'u4'),
    ('A', '28i4'),
    ('B', '28i4'),
    ('C', '28i4'),
    ('D', '28i4'),
    ('X', '28i4'),
    ('Y', '28i4'),
    ('tl', '28i4'),
    ('th', '28i4'),
    ('crc', 'u4'),
    ('_junk', 'u8'),
])
dtype_new = dtype.newbyteorder('>')
assert dtype.itemsize==16+912, dtype.itemsize


def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('output', metavar='H5FILE', help='.h5 file')
    P.add_argument('input', nargs='+', metavar='DATFILE', help='.dat file(s)')
    P.add_argument('-W', '--overwrite', action='store_const', dest='mode', const='w', default='x',
                   help='Overwrite existing output file.  Default no')
    return P

def main(args):
    OF = h5py.File(args.output, args.mode)
    h5cols = ('msgid', 'sec', 'nsec', 'seq', 'A', 'B', 'C', 'D', 'X', 'Y', 'tl', 'th', 'crc')
    DSs = {}
    idx = 0

    for col in h5cols:
        nt = dtype[col]
        if nt.subdtype is None: # scalar
            DSs[col] = OF.create_dataset(col, shape=(0,), maxshape=(None,), dtype=nt)
        else:
            subtype, subshape = nt.subdtype
            DSs[col] = OF.create_dataset(col, shape=(0,)+subshape, maxshape=(None,)+subshape, dtype=subtype)

    for fname in args.input:
        buf = []
        print('start', fname)
        with open(fname, 'rb') as F:
            # find size of first message
            PS, msgid, blen, sec, ns = header.unpack(F.read(header.size))
            assert PS==b'PS', PS
            assert blen==912, blen
            msgsize = header.size + blen
            assert msgsize==dtype.itemsize, (msgsize, dtype.itemsize)
            F.seek(0)

            while True:
                raw = F.read(msgsize*flush_batch)
                if not raw:
                    break
                arr = numpy.frombuffer(raw, dtype=dtype_new)

                # check message alignment
                assert (arr['ps']==0x5053).all(), arr['ps']
                assert (arr['msgid']==12345).all(), arr['ps']
                assert (arr['msglen']==dtype.itemsize-16).all(), arr['msglen']

                for col, DS in DSs.items():
                    DS.resize((idx + arr.shape[0],)+DS.shape[1:])
                    DS[idx:] = arr[col]

                idx += arr.shape[0]

if __name__=='__main__':
    main(getargs().parse_args())

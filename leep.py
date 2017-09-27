#!/usr/bin/env python

from __future__ import print_function

import logging
_log = logging.getLogger(__name__)

import sys, socket, struct, random, zlib, json

import numpy

be32 = numpy.dtype('>u4')
be16 = numpy.dtype('>u2')

def readwrite(args, dev):
    names, addrs, values = [], [], []
    for AV in args.addr:
        addr, _sep, value = AV.partition('=')
        if value:
            value = int(value,0)
        else:
            value = None

        # attempt to lookup register name
        info = dev.json.get(addr)
        if info is not None:
            name, base = addr, info['base_addr']
            for offset in range(0, 1<<info.get('addr_width',0)):
                names.append('%s[%d]'%(name, offset))
                addrs.append(base+offset)
                # TODO: write same value to every address
                values.append(value)

        else:
            names.append(addr)
            addrs.append(int(addr, 0))
            values.append(value)

    values = dev.exchange(addrs, values)

    for name, value in zip(names, values):
        print("%s \t%08x"%(name, value))

def listreg(args, dev):
    json.dump(dev.json, sys.stdout, indent=2)
    sys.stdout.write('\n')

def dumpaddrs(args, dev):
    addrs = []
    for info in dev.json.values():
        base = info['base_addr']
        for addr in range(0, 1<<info.get('addr_width',0)):
            addrs.append(base+addr)

    values = dev.exchange(addrs)

    for addr, value in zip(addrs, values):
        if value==0 and args.ignore_zeros:
            continue
        print("%08x %08x"%(addr, value))


def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('-d','--debug',action='store_const', const=logging.DEBUG, default=logging.INFO)
    P.add_argument('-q','--quiet',action='store_const', const=logging.WARN, dest='debug')
    P.add_argument('-l','--list', action='store_true', help='List register names')
    P.add_argument('-t','--timeout', type=float, default=1.0)
    P.add_argument('dest', metavar="host[:port]", help="Server address")

    SP = P.add_subparsers()

    S = SP.add_parser('reg', help='read/write registers')
    S.set_defaults(func=readwrite)
    S.add_argument('addr', nargs='+', help="address/register name with optional value to write")

    S = SP.add_parser('list', help='list registers')
    S.set_defaults(func=listreg)

    S = SP.add_parser('dump', help='dump registers')
    S.add_argument('-Z','--ignore-zeros', action='store_true', help="Only print registers with non-zero values")
    S.set_defaults(func=dumpaddrs)

    return P.parse_args()

class Device(object):
    def __init__(self, args):
        host, _sep, port = args.dest.partition(':')
        self.dest = (host, int(port or '50006'))

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)
        self.sock.settimeout(args.timeout)

        self._readrom()

    def _exchange(self, addrs, values=None):

        pad = None
        if len(addrs)<3:
            pad = 3-len(addrs)
            addrs.extend([0]*pad)
            values.extend([None]*pad)

        msg = numpy.zeros(2+2*len(addrs), dtype=be32)
        msg[0] = random.randint(0,0xffffffff)
        msg[1] = msg[0]&0xffffffff

        for i,(A, V) in enumerate(zip(addrs, values), 1):
            A &= 0x00ffffff
            if V is None:
                A |= 0x10000000
            msg[2*i] = A
            msg[2*i+1] = V or 0

        tosend = msg.tostring()
        _log.debug("%s Send (%d) %s", self.dest, len(tosend), repr(tosend))
        self.sock.sendto(tosend, self.dest)

        while True:
            reply, src = self.sock.recvfrom(1024)
            _log.debug("%s Recv (%d) %s", src, len(reply), repr(reply))

            if len(reply)%8:
                reply = reply[:-(len(reply)%8)]

            if len(tosend)!=len(reply):
                _log.error("Reply truncated %d %d", len(tosend), len(reply))
                continue

            reply = numpy.fromstring(reply, be32)
            if (msg[:2]!=reply[:2]).any():
                _log.error('Ignore reply w/o matching nonce %s %s', msg[:2], reply[:2])
                continue
            elif (msg[2::2]!=reply[2::2]).any():
                _log.error('reply addresses are out of order')
                continue

            break

        ret = reply[3::2]
        if pad:
            ret = ret[:-pad]
        return ret

    def exchange(self, addrs, values=None):
        """Accepts a list of address and values (None to read).
        Returns a numpy.ndarray in the same order.
        """

        if values is None:
            values = [None]*len(addrs)

        ret = numpy.zeros(len(addrs), be32)
        for i in range(0, len(addrs), 127):
            A, B = addrs[i:i+127], values[i:i+127]

            P = self._exchange(A, B)
            ret[i:i+127] = P

        return ret

    def _readrom(self):
        values = self.exchange(range(0x800, 0x1000))

        values = numpy.frombuffer(values, be16)
        _log.debug("ROM[0] %08x", values[0])
        values = values[1::2] # discard upper bytes

        self.json = None
        while len(values):
            type = values[0]>>14
            size = values[0]&0x3fff
            _log.debug("ROM Descriptor type=%d size=%d", type, size)

            if type==0:
                break

            blob, values = values[1:size+1], values[size+1:]
            if len(blob)!=size:
                raise ValueError("Truncated ROM Descriptor")

            if type==1:
                blob = blob.tostring()
                _log.info("ROM Text '%s'", blob)
            elif type==2:
                blob = ''.join(["%04x"%b for b in blob])
                _log.info("ROM Hash %s", blob)
            elif type==3:
                self.json = json.loads(zlib.decompress(blob.tostring()))

        if self.json is None:
            raise RuntimeError('ROM contains no JSON')


def main(args):
    dev = Device(args)
    args.func(args, dev)

if __name__=='__main__':
    args = getargs()
    logging.basicConfig(level=args.debug)
    main(args)

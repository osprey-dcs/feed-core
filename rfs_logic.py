#!/usr/bin/env python

"""
PRC simulation logic for use with

src/Db/rfs_logic.substitutions
"""

import logging
_log = logging.getLogger(__name__)

import time

import numpy as np
twopi = 2.0*np.pi

from leeputil import Device

class WFLogic(object):
    def __init__(self, args, dev, inst=0):
        self.dev, self.inst = dev, inst
        self.reg_chan_keep = dev.json['shell_%d_dsp_chan_keep'%inst]
        self.reg_tag = dev.json['shell_%d_dsp_tag'%inst]
        self.reg_circle = dev.json['shell_%d_circle_data'%inst]
        self.reg_slow = dev.json['shell_%d_slow_data'%inst]

        self.circle = np.zeros(2**self.reg_circle['addr_width'], dtype='u4')
        self.slow   = np.zeros(2**self.reg_slow['addr_width'], dtype='u4')
        self.phase = 0.0

    def loop_start(self):
        _log.info('loop_start %d', self.inst)
        tag, self.keep = self.dev.exchange([
            self.reg_tag['base_addr'], self.reg_chan_keep['base_addr'],
        ])

        self.slow[33] = tag

    def loop_end(self):
        _log.info('loop_end %d', self.inst)
        tag = self.dev.exchange([self.reg_tag['base_addr']])[0]

        self.slow[34] = tag

        N, keep = 0, self.keep
        while keep:
            if keep&1:
                N += 1
            keep >>= 1

        self.circle[:] = 0

        if N==0:
            return

        W = twopi/512
        T = np.arange(self.circle.shape[0]/N)
        sig = (
            np.sin(W*T+ np.deg2rad( 0.0)+self.phase), np.cos(W*T+ np.deg2rad( 0.0)+self.phase),
            np.sin(W*T+ np.deg2rad(20.0)+self.phase), np.cos(W*T+ np.deg2rad(20.0)+self.phase),
            np.sin(W*T+ np.deg2rad(40.0)+self.phase), np.cos(W*T+ np.deg2rad(40.0)+self.phase),
            np.sin(W*T+ np.deg2rad(60.0)+self.phase), np.cos(W*T+ np.deg2rad(60.0)+self.phase),
        )

        i = 0
        for n in range(8):
            if not self.keep&(0x800>>n):
                continue
            self.circle[i:N*sig[n].shape[0]:N] = sig[n] * 1024
            i+=1

        self.dev.exchange(
            list(self.reg_circle['base_addr']+np.arange(self.circle.shape[0]))
        , list(self.circle))
        self.dev.exchange(
            list(self.reg_slow['base_addr']+np.arange(self.slow.shape[0]))
        , list(self.slow))
        _log.info('circle[:20] = %s', self.circle[:20])

        self.phase = (self.phase + np.deg2rad(5.0)) % twopi

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('dest', metavar="host[:port]", help="Server address")
    P.add_argument('-d','--debug',action='store_const', const=logging.DEBUG, default=logging.INFO)
    P.add_argument('-t','--timeout', type=float, default=1.0)
    return P.parse_args()

def main(args):
    dev = Device(args)
    addr_flip = dev.json['circle_buf_flip']['base_addr']
    addr_ready = dev.json['llrf_circle_ready']['base_addr']

    logic = (WFLogic(args, dev, 0), WFLogic(args, dev, 1))
    try:
        while True:
            time.sleep(1.0)
            _log.debug("Loop")
 
            # possible race here as we have no atomic clear op
            flip, ready = dev.exchange([
                addr_flip, addr_ready,
            ],[
                None, None,
            ])
            _log.debug("Loop1 flip=%x ready=%x", flip, ready)

            # clear ready for units to acquire
            ready &= ~flip
            dev.exchange([
                addr_ready,
            ],[
                ready,
            ])
            _log.debug("Loop2 flip=%x ready=%x", flip, ready)

            for i,D in enumerate(logic):
                if flip&(1<<i):
                    D.loop_start()

            time.sleep(0.05)

            for i,D in enumerate(logic):
                if flip&(1<<i):
                    D.loop_end()

            ready |= flip
            flip = 0
            dev.exchange([
                addr_flip, addr_ready,
            ],[
                flip, ready,
            ])
            _log.debug("Loop3 flip=%x ready=%x", flip, ready)

    except KeyboardInterrupt:
        pass

if __name__=='__main__':
    args = getargs()
    logging.basicConfig(level=args.debug)
    main(args)

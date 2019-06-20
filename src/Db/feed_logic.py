#!/usr/bin/env python3
"""
FEED acquisition device logic substitution generator

{
    "signal_group":{
        # required
        "reset":{"name":"reg_reset","bit": 0}
        ,"status":{"name":"reg_status","bit": 1	}
        # optional
        ,"readback":{
            "scalar1":{"name":"reg_name"}
            ,"wf1":{
                "name":"reg_name1"
                ,"max_size":8196,
                ,"mask":"mask_reg"
                ,"signals":[
                    {"name":"CH1"}
                ]
            }
        }
    }
}
"""

from __future__ import print_function

import json, re, itertools
from collections import OrderedDict

try:
    from itertools import izip as zip
except ImportError:
    pass

try:
    from cStringIO import StringIO
except ImportError:
    from io import StringIO

def strip_comments(inp):
    return re.sub(r'#.*$', '', inp, flags=re.MULTILINE)

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('json', help='json config file')
    P.add_argument('output', help='output .substitution file')
    return P.parse_args()

def batchby(it, cnt):
    grp = []
    for item in it:
        grp.append(item)
        if len(grp)==cnt:
            yield grp
            grp = []
    if len(grp):
        yield grp

class Main(object):
    def __init__(self, args):
        with open(args.json, 'r') as F:
            raw = F.read()

        cooked = strip_comments(raw)

        try:
            conf = json.loads(cooked)
        except:
            print("Error parsing JSON")
            print("======")
            print(cooked)
            raise

        # {"file.template": [('macro', 'value'), ...], ...}
        self.out = OrderedDict([
            ('feed_logic_trigger.template', []),
            ('feed_logic_read.template', []),
            ('feed_logic_array_mask.template', []),
            ('feed_logic_fanout.template', []),
            ('feed_logic_signal.template', []),
        ])

        for gname, gconf in conf.items():
            #out.write('### Start Signal Group: %s\n#\n'%gname)
            #for line in json.dumps(gconf, indent='  ').splitlines():
            #    out.write('%s\n'%line)

            self.signal_group(gname, gconf)

            #out.write('\n### End Signal Group: %s\n'%name)

        fd = StringIO()
        fd.write("# Generated from:\n")
        for line in raw.splitlines():
            fd.write('# %s\n'%line)
        fd.write("\n")

        for fname, lines in self.out.items():
            if not lines:
                fd.write("\n# no %s\n"%fname)
                continue

            fd.write("""
file "%s"
{
"""%fname)

            lines.reverse()
            for ent in lines:
                fd.write('{' + ', '.join(['%s="%s"'%(k,v) for k, v in ent.items()]) + '}\n')

            fd.write("}\n")

        with open(args.output, 'w') as F:
            F.write(fd.getvalue())

    def signal_group(self, gname, gconf):
        # we append template blocks in reverse order to simplify accounting of next record.
        # start with the last link in the chain, which then re-arms
        nextrec = '$(PREF)%sREARM'%gname

        # de-mux of signals from array registers.
        # these will be synchronously processed through a set of fanouts
        fanout2 = []
        for rname, rconf in gconf.get('readback', {}).items():
            signals = rconf.get('signals', [])
            mask = hex((1<<len(signals))-1)

            if mask and 'mask' in rconf:
                # this register has a mask
                ent = OrderedDict([
                    ('BASE', '$(PREF)%s%s:'%(gname, rname)),
                    ('REG', rconf['mask'])
                ])
                mask = ent['BASE']+'MASK CP MSI'
                self.out['feed_logic_array_mask.template'].append(ent)

            for idx, signal in enumerate(signals):
                ent = OrderedDict([
                    ('BASE', '$(PREF)%s%s:%s'%(gname, rname, signal['name'])),
                    ('REG', rconf['name']),
                    ('SIZE', str(rconf.get('max_size', 8196))),
                    ('IDX', str(idx)),
                    ('MASK', mask),
                ])
                fanout2.append(ent['BASE']+'E_')
                self.out['feed_logic_signal.template'].append(ent)

        nextfo = itertools.count(1)
        fanout2 = list(batchby(fanout2, 6))
        fanout2.reverse()

        # emit fanouts to process all signals
        for records, idx in zip(fanout2, nextfo):
            ent = OrderedDict([
                ('NAME', '$(PREF)%sFO%d_'%(gname, idx)),
            ])

            for n, record in enumerate(records, 1):
                ent['LNK%d'%n] = record
            ent['FLNK'] = nextrec
            nextrec = ent['NAME']

            self.out['feed_logic_fanout.template'].append(ent)

        # read back registers
        for rname, rconf in gconf.get('readback', {}).items():
            ent = OrderedDict([
                ('BASE', '$(PREF)%s%s'%(gname, rname)),
                ('REG', rconf['name']),
                ('FLNK', nextrec),
            ])
            nextrec = ent['BASE']+'E_'
            self.out['feed_logic_read.template'].append(ent)


        # finally the beginning
        self.out['feed_logic_trigger.template'].append(OrderedDict([
            ('BASE', '$(PREF)'+gname),
            ('ARM_REG', gconf['reset']['name']),
            ('ARM_MASK', hex(1<<gconf['reset']['bit'])),
            ('RDY_REG', gconf['status']['name']),
            ('RDY_MASK', hex(1<<gconf['status']['bit'])),
            ('NEXT', nextrec),
        ]))


if __name__=='__main__':
    args = getargs()
    Main(args)

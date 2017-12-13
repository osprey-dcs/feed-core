#!/usr/bin/env python

import os, logging, json

datadir = os.path.dirname(__file__)

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('-d','--debug',action='store_const', const=logging.DEBUG, default=logging.INFO)
    P.add_argument('-q','--quiet',action='store_const', const=logging.WARN, dest='debug')
    P.add_argument('--prefix', default='TST:reg:')
    P.add_argument('output')
    P.add_argument('pvlist')
    return P.parse_args()

def main(args):
    import xml.etree.ElementTree as ET
    T = ET.parse(os.path.join(datadir, 'base.opi')).getroot()

    with open(args.pvlist, 'r') as F:
        blob = json.load(F)

    # sort by register name for stability
    blob = list(blob.items())
    blob.sort(key=lambda i:i[0])

    nextrow = 0
    for name, info in blob:
        if info.get('addr_width',0)!=0:
            continue
        W = ET.parse(os.path.join(datadir, 'TextUpdate.opi')).getroot()

        read_pv_name = "%s%s-I"%(args.prefix, name)
        set_pv_name = "%s%s-SP"%(args.prefix, name)

        NY = 0
        for W in list(W.findall('widget')):
            Y = int(W.find('y').text)
            W.find('y').text = str(Y+nextrow)

            NY = max(NY, int(W.find('height').text))

            if W.find('name').text in ['Label', 'Readback', 'Setting']:
                W.find('text').text = name

            if W.find('name').text in ['Readback', 'Scan']:
                W.find('pv_name').text = read_pv_name
            elif W.find('name').text in ['Setting']:
                W.find('pv_name').text = set_pv_name

            T.append(W)

        nextrow += NY

    with open(args.output,'wb') as F:
        F.write(b'<?xml version="1.0" encoding="UTF-8"?>\n')
        F.write(ET.tostring(T))

if __name__=='__main__':
    args = getargs()
    logging.basicConfig(level=args.debug)
    main(args)

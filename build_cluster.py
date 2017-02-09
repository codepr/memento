#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Author: Andrea Giacomo Baldan
# License: MIT
# Description: Create cluster configurations for a defined number of nodes

from glob import glob
import os
import sys
import uuid


def main():
    """
    Main access point, parse arguments and create configuration files needed
    to form a cluster
    """

    # remove any conf file to avoid inconsistencies
    for filename in glob("*.conf"):
        os.remove(filename)

    # generate random uuids for each node
    uuids = {}
    for x in sys.argv[1:]:
        uid = uuid.uuid4()
        uuids[uid.hex] = x

    # write ip address, port and names to each conf file
    for idx, key in enumerate(uuids):
        count = 0
        with open("node{}.conf".format(idx), "a+") as fh:
            fh.write("# node{} configuration file\n".format(idx))
            for k, v in uuids.items():
                self = "0"
                peer = v.split(":")
                if count == idx:
                    self = "1"
                fh.write(peer[0] + "\t" + peer[1] + "\t" + k + "\t" + self + "\n")
                count += 1

            fh.close()



if __name__ == '__main__':
    exit(main())

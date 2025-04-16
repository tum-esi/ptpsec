#!/usr/bin/env python

from sys import argv

from mininet.cli import CLI
from mininet.net import Mininet
from mininet.node import RemoteController
from mininet.link import TCULink
from mininet.term import makeTerms
from mininet.node import OVSSwitch

#import random

def eval_topo(net: Mininet):
    M = net.addHost('M')
    S = net.addHost('S')

    s1 = net.addSwitch('s1', cls=OVSSwitch, datapath='user')
    s2 = net.addSwitch('s2', cls=OVSSwitch, datapath='user')
    s3 = net.addSwitch('s3', cls=OVSSwitch, datapath='user')
    s4 = net.addSwitch('s4', cls=OVSSwitch, datapath='user')
    s5 = net.addSwitch('s5', cls=OVSSwitch, datapath='user')
    s6 = net.addSwitch('s6', cls=OVSSwitch, datapath='user')
    s7 = net.addSwitch('s7', cls=OVSSwitch, datapath='user')
    s8 = net.addSwitch('s8', cls=OVSSwitch, datapath='user')
    s9 = net.addSwitch('s9', cls=OVSSwitch, datapath='user')
    s10 = net.addSwitch('s10', cls=OVSSwitch, datapath='user')
    s11 = net.addSwitch('s11', cls=OVSSwitch, datapath='user')
    s12 = net.addSwitch('s12', cls=OVSSwitch, datapath='user')

    net.addLink(M, s1)
    net.addLink(M, s5)
    net.addLink(M, s9)

    net.addLink(s1, s2)
    net.addLink(s1, s5)
    net.addLink(s1, s6)
    # net.addLink(s1, s6, cls=TCULink, params1={'delay': '10ms'}, params2={'delay': '0ms'})
    net.addLink(s5, s2)
    net.addLink(s9, s10)

    net.addLink(s2, s6)
    net.addLink(s2, s7)
    net.addLink(s6, s3)
    # net.addLink(s6, s3, cls=TCULink, params1={'delay': '20ms'}, params2={'delay': '0ms'})
    net.addLink(s6, s10)
    net.addLink(s6, s11)

    # net.addLink(s3, s4)
    net.addLink(s3, s4, cls=TCULink, params1={'delay': '0ms'}, params2={'delay': '20ms'})
    net.addLink(s3, s7)
    net.addLink(s7, s8)
    net.addLink(s7, s12)
    net.addLink(s11, s12)

    net.addLink(s4, s8)

    net.addLink(s4, S)
    net.addLink(s8, S)
    net.addLink(s12, S)

def setup_ips(net: Mininet):
    import re
    for host in net.hosts:
        for intf in host.intfList():
            m = re.fullmatch(r"h([0-9]+)", host.name)

            if not m:
                print("invalid host name, can't set IPs")
                break

            host_id = m.group(1)

            host.setIP(f'10.0.0.{host_id}', intf=intf)


if '__main__' == __name__:
    DEFAULT_TOPO = eval_topo

    topos = {'A': eval_topo,
            }

    topo = DEFAULT_TOPO

    if len(argv) > 1:
        if argv[1] in topos:
            topo = topos.get(argv[1])
        else:
            print(f"Warning: {argv[1]} is an invalid topology name. Using default topology instead")

    net = Mininet(controller=RemoteController)

    c0 = net.addController('c0', port=6633)

    topo(net)

    net.build()

    setup_ips(net)

    c0.start()

    for switch in net.switches:
        switch.start([c0])
        switch.cmd(f"ovs-vsctl set Bridge {switch.name} protocols=OpenFlow13")

    net.terms += makeTerms(net.controllers, 'controller')
    net.terms += makeTerms(net.hosts, 'host')

    CLI(net)

    net.stop()

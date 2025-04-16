import time

from os_ken.base import app_manager
from os_ken.base.app_manager import lookup_service_brick
from os_ken.controller import ofp_event
from os_ken.controller.handler import MAIN_DISPATCHER, set_ev_cls
from os_ken.ofproto import ofproto_v1_3
from os_ken.lib.packet.packet import Packet
from os_ken.lib.packet.lldp import lldp
from os_ken.topology.switches import Switches
from os_ken.topology.api import get_switch

from sdn_controllers.topology_data import TopologyData

import util
import logging
logger = util.get_logger(__name__, logging.INFO)

class DelayMonitor(app_manager.OSKenApp):
    """
    This controller manages all regular (i.e. non ptp-related) packages and acts as a regular learning
    switch but also uses the minimum spanning tree of TopologyData to avoid loops
    """

    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    def __init__(self, *args, **kwargs):
        super(DelayMonitor, self).__init__(*args, **kwargs)
        self.name = 'delay_monitor'

        self.topology_data: TopologyData = lookup_service_brick('topology_data')
        self.switches_module: Switches = lookup_service_brick('switches')

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def packet_in_handler(self, ev):
        recv_timestamp_ns = time.time_ns()
        recv_timestamp_s = time.time()

        msg = ev.msg

        pkt: Packet = Packet(msg.data)
        lldp_pkt: lldp = pkt.get_protocol(lldp)

        if not lldp_pkt:
            return

        # WARNING: this is probably not reliable and only works with my edited os_ken version
        custom_pkt = len(pkt.data) > 60

        delay_ms = None

        if custom_pkt:
            sent_timestamp_ns = int.from_bytes(pkt.data[-8:], 'big')
            delay_ms = (recv_timestamp_ns - sent_timestamp_ns) / 1e6

        if self.switches_module is None:
            self.switches_module = lookup_service_brick('switches')

        src = int(lldp_pkt.tlvs[0].chassis_id.decode()[len('dpid:'):], base=16)
        src_port = int.from_bytes(lldp_pkt.tlvs[1].port_id, "big")

        dst = msg.datapath.id
        logger.debug(f"LLDP packet recieved at {dst}")

        src_switch = get_switch(self, dpid=src)
        if src_switch is None or len(src_switch) < 1:
            return

        src_switch = src_switch[0]
        for port in src_switch.ports:
            if port.port_no == src_port:
                if not custom_pkt:
                    sent_timestamp_s = self.switches_module.ports[port].timestamp
                    if sent_timestamp_s:
                        delay_ms = (recv_timestamp_s - sent_timestamp_s) * 1000

                if delay_ms is not None and self.topology_data.graph.has_edge(src, dst):
                    # NOTE: this does not take the delay between the switch and the controller into account
                    self.topology_data.graph[src][dst]['delay'] = delay_ms
                    logger.debug(f"{src} -> {dst}: {delay_ms}")

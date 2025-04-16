import os_ken
from os_ken.base import app_manager
from os_ken.base.app_manager import lookup_service_brick
from os_ken.controller import ofp_event
from os_ken.controller.controller import Datapath
from os_ken.controller.handler import CONFIG_DISPATCHER, MAIN_DISPATCHER, set_ev_cls
from os_ken.ofproto import ofproto_v1_3
from os_ken.lib.packet import packet, ethernet

from sdn_controllers.topology_data import TopologyData
from ptp.ptp_packet import PtpPacket
from ptp.ptp_message_types import MessageType

from util import is_multicast

import util
import logging
logger = util.get_logger(__name__, logging.INFO)

class RegularSwitch(app_manager.OSKenApp):
    """
    This controller manages all regular (i.e. non ptp-related) packages and acts as a regular learning
    switch but also uses the minimum spanning tree of TopologyData to avoid loops
    """

    MULTICAST_FLOW_COOKIE = 0x1
    SIMPLE_SWITCH_FLOW_COOKIE = 0x1 << 1
    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    def __init__(self, *args, **kwargs):
        super(RegularSwitch, self).__init__(*args, **kwargs)
        self.name = 'regular_switch'

        self.mac_to_port = {}

        self.topology_data: TopologyData = lookup_service_brick('topology_data')

        self.TOPO_DISCOVERY_INIT_TIME = 10

    @set_ev_cls(ofp_event.EventOFPSwitchFeatures, CONFIG_DISPATCHER)
    def switch_features_handler(self, ev: ofp_event.EventOFPSwitchFeatures):
        datapath: Datapath = ev.msg.datapath
        ofproto: ofproto_v1_3 = datapath.ofproto
        parser = datapath.ofproto_parser

        # install the table-miss flow entry
        match = parser.OFPMatch()
        actions = [parser.OFPActionOutput(
            ofproto.OFPP_CONTROLLER, ofproto.OFPCML_NO_BUFFER)]
        self.add_flow(datapath, 0, match, actions, 0)

    def add_flow(self, datapath, priority, match, actions, cookie=0):
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        # construct flow_mod message and send it
        inst = [parser.OFPInstructionActions(
            ofproto.OFPIT_APPLY_ACTIONS, actions)]

        mod = parser.OFPFlowMod(datapath=datapath,
                                cookie=cookie,
                                priority=priority,
                                match=match,
                                instructions=inst)

        datapath.send_msg(mod)

    # from https://sourceforge.net/p/ryu/mailman/message/32333352/
    def delete_all_flows(self, datapath: Datapath):
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        empty_match = parser.OFPMatch()
        instructions = []

        logger.info(f"deleting flows for dpid {datapath.id}")

        # mod = parser.OFPFlowMod(datapath=datapath,
        #                         command=ofproto.OFPFC_DELETE,
        #                         match=empty_match,
        #                         priority=1,
        #                         out_port=ofproto.OFPP_ANY,
        #                         instructions=instructions)
        # datapath.send_msg(mod)

        mod = parser.OFPFlowMod(datapath=datapath,
                                table_id=ofproto.OFPTT_ALL,
                                command=ofproto.OFPFC_DELETE,
                                cookie=self.MULTICAST_FLOW_COOKIE,  # | self.SIMPLE_SWITCH_FLOW_COOKIE,
                                cookie_mask=self.MULTICAST_FLOW_COOKIE | self.SIMPLE_SWITCH_FLOW_COOKIE,
                                priority=5,
                                out_port=ofproto.OFPP_ANY,
                                out_group=ofproto.OFPG_ANY)
        # instructions=instructions)
        datapath.send_msg(mod)

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def _packet_in_handler(self, ev):
        msg = ev.msg
        datapath: Datapath = ev.msg.datapath
        ofproto: ofproto_v1_3 = datapath.ofproto
        parser: os_ken.ofproto.ofproto_v1_3_parser = datapath.ofproto_parser

        # get Datapath ID to identify OpenFlow switches
        dpid = datapath.id
        self.mac_to_port.setdefault(dpid, {})

        # analyse the received packets using the packet library
        pkt: packet.Packet = packet.Packet(msg.data)
        eth_pkt: ethernet.ethernet = pkt.get_protocol(ethernet.ethernet)
        dst = eth_pkt.dst
        src = eth_pkt.src

        # ignore LLDP packages
        if eth_pkt.ethertype == 0x88cc:
            return

        # get the received port number from packet_in message
        in_port = msg.match['in_port']

        logger.debug("packet in %s %s %s %s", dpid, src, dst, in_port)

        is_ptp: bool = eth_pkt.ethertype == 0x88F7

        if is_ptp:
            ptp_pkt: PtpPacket = PtpPacket(pkt.protocols[-1])
            # ignore these message types after the topology init phase as they will be handled by
            # the ptpsec_controller
            if (self.topology_data.topo_loop_uptime > self.TOPO_DISCOVERY_INIT_TIME
                and (ptp_pkt.messageType in
                     [MessageType.SYNC,
                      MessageType.FOLLOW_UP,
                      MessageType.DELAY_REQ,
                      MessageType.DELAY_RESP,
                      MessageType.MEASUREMENT])):
                return

        # learn a mac address to avoid FLOOD next time
        self.mac_to_port[dpid][src] = in_port

        # handle multicast packages with minimum spanning tree to avoid loops
        if is_multicast(dst):
            actions = []
            if dpid in self.topology_data.min_spanning_tree:
                for edge in self.topology_data.min_spanning_tree[dpid].values():
                    port = edge['ports'][dpid]
                    if port != in_port:
                        actions.append(parser.OFPActionOutput(port))

            # don't set a flow for ptp packages so that we keep receiving them
            # also don't set flows if the topology discovery has not finished
            if not is_ptp and self.topology_data.topo_loop_uptime > self.TOPO_DISCOVERY_INIT_TIME:
                # import pdb; pdb.set_trace()
                match = parser.OFPMatch(in_port=in_port, eth_dst=dst)
                self.add_flow(datapath, 2, match, actions, self.MULTICAST_FLOW_COOKIE)

            if not actions:
                return

            out = parser.OFPPacketOut(datapath=datapath,
                                      buffer_id=ofproto.OFP_NO_BUFFER,
                                      in_port=in_port,
                                      actions=actions,
                                      data=msg.data)
            datapath.send_msg(out)
            return

        # if the destination mac address in already learned,
        # decide which port to output the packet, otherwise FLOOD
        if dst in self.mac_to_port[dpid]:
            out_port = self.mac_to_port[dpid][dst]
        else:
            out_port = ofproto.OFPP_FLOOD

        # construct action list
        actions = [parser.OFPActionOutput(out_port)]

        # install a flow to avoid packet_in next time
        if out_port != ofproto.OFPP_FLOOD:
            match = parser.OFPMatch(in_port=in_port, eth_dst=dst)
            self.add_flow(datapath, 1, match, actions, self.SIMPLE_SWITCH_FLOW_COOKIE)

        # construct packet_out message and send it
        out = parser.OFPPacketOut(datapath=datapath,
                                  buffer_id=ofproto.OFP_NO_BUFFER,
                                  in_port=in_port,
                                  actions=actions,
                                  data=msg.data)
        datapath.send_msg(out)

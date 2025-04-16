import os_ken
from os_ken.base import app_manager
from os_ken.base.app_manager import lookup_service_brick
from os_ken.controller import ofp_event
from os_ken.controller.controller import Datapath
from os_ken.controller.handler import MAIN_DISPATCHER, set_ev_cls
from os_ken.ofproto import ofproto_v1_3
from os_ken.lib.packet import packet, ethernet
from os_ken.lib import hub

import networkx as nx

from ptp.ptp_host import PtpHost, PtpPath
from ptp.ptp_packet import PtpPacket
from ptp.ptp_message_types import MessageType, MeasurementType
from sdn_controllers.topology_data import TopologyData

from settings import REQUIRED_REDUNDANT_PATHS
from util import get_n_redundant_paths

import util
import logging
logger = util.get_logger(__name__, logging.INFO, False)

PTP_ETH_TYPE = 0x88F7

class PTPSecController(app_manager.OSKenApp):
    """
    This controller handles all ptp (and especially ptpsec) packages and is responsible for
    monitoring the current network security aswell as routing the packages over redundant paths
    """

    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    def __init__(self, *args, **kwargs):
        super(PTPSecController, self).__init__(*args, **kwargs)
        self.name = 'ptpsec_controller'

        # map from clock identity to host object
        self.ptp_hosts: dict[int, PtpHost] = {}

        # clock identity of the current master
        self.ptp_master: int = None
        self.master_main_port: str = None

        self.topology_data: TopologyData = lookup_service_brick('topology_data')

        self.clock_graph: nx.Digraph = nx.DiGraph()

        self.ptpsec_info_thread = hub.spawn(self._ptpsec_info_loop)

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

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def _packet_in_handler(self, ev):
        msg = ev.msg
        datapath: Datapath = ev.msg.datapath
        ofproto: ofproto_v1_3 = datapath.ofproto
        parser: os_ken.ofproto.ofproto_v1_3_parser = datapath.ofproto_parser

        pkt: packet.Packet = packet.Packet(msg.data)
        eth_pkt = pkt.get_protocol(ethernet.ethernet)
        src_mac = eth_pkt.src
        in_port = msg.match['in_port']

        # ignore non ptp packets
        if eth_pkt.ethertype != PTP_ETH_TYPE:
            return

        ptp_pkt = PtpPacket(pkt.protocols[-1])
        src_clockIdentity = ptp_pkt.sourceClockIdentity

        if src_clockIdentity not in self.ptp_hosts:
            self.ptp_hosts[src_clockIdentity] = PtpHost(src_clockIdentity)

        self.ptp_hosts[src_clockIdentity].mac_to_portid[src_mac] = ptp_pkt.sourcePortNumber

        if ptp_pkt.messageType not in [MessageType.SYNC, MessageType.FOLLOW_UP,
                                       MessageType.DELAY_REQ, MessageType.DELAY_RESP,
                                       MessageType.MEASUREMENT]:
            return

        if ptp_pkt.messageType == MessageType.SYNC:
            self.ptp_master = src_clockIdentity
            self.master_main_port = src_mac

        logger.debug(f"ptp packet from {src_clockIdentity} of type {ptp_pkt.messageType} at dp {
            datapath.id} in port {in_port}")

        if ptp_pkt.messageType == MessageType.MEASUREMENT:
            logger.debug(f"{ptp_pkt.msg.measType=}")

        # routing of the package
        # NOTE TODO: this has A LOT of duplicat code that can be broken in to smaller functions
        #            further more, the differentiation between main_path and all other paths might
        #            not be neccessary.
        actions = []
        match = None
        dbg_paths = []
        if ptp_pkt.messageType in [MessageType.SYNC, MessageType.FOLLOW_UP]:
            # sync and followup messages are ment for all slaves
            for host in self.ptp_hosts.values():
                host: PtpHost
                if host.clock_identity == self.ptp_master or host.main_path is None:
                    continue

                out_port = self.get_out_port(datapath.id, host.main_path, True)

                if out_port is not None:
                    actions.append(parser.OFPActionOutput(out_port))
                    match = parser.OFPMatch(in_port=in_port,
                                            eth_type_nxm=PTP_ETH_TYPE,
                                            ptp_msg_type=ptp_pkt.messageType.value)
                    dbg_paths.append(host.main_path.path)

        elif ptp_pkt.messageType == MessageType.DELAY_RESP:
            # delay_resp messages are only ment for the requesting slave
            host: PtpHost = self.ptp_hosts.get(ptp_pkt.msg.requestingClockIdentity, None)

            if host is None or host.main_path is None:
                return

            out_port = self.get_out_port(datapath.id, host.main_path, True)

            if out_port is not None:
                actions.append(parser.OFPActionOutput(out_port))
                match = parser.OFPMatch(in_port=in_port,
                                        eth_type_nxm=PTP_ETH_TYPE,
                                        ptp_msg_type=ptp_pkt.messageType.value,
                                        ptp_dr_requesting_clock_id=ptp_pkt.msg.requestingClockIdentity)
                dbg_paths.append(host.main_path.path)

        elif ptp_pkt.messageType == MessageType.DELAY_REQ:
            # delay_request are ment for the master
            host: PtpHost = self.ptp_hosts[src_clockIdentity]

            if host.main_path is None:
                return

            out_port = self.get_out_port(datapath.id, host.main_path, False)

            if out_port is not None:
                actions.append(parser.OFPActionOutput(out_port))
                match = parser.OFPMatch(in_port=in_port,
                                        eth_type_nxm=PTP_ETH_TYPE,
                                        ptp_msg_type=ptp_pkt.messageType.value,
                                        ptp_src_clock_id=src_clockIdentity)
                dbg_paths.append(host.main_path.path)

        elif ptp_pkt.messageType == MessageType.MEASUREMENT:
            if ptp_pkt.msg.measType in [MeasurementType.MEAS_MEASUREMENT, MeasurementType.MEAS_FOLLOW_UP]:
                if src_clockIdentity == self.ptp_master:
                    host: PtpHost = self.ptp_hosts.get(ptp_pkt.msg.targetClockIdentity, None)

                    if host is None:
                        return

                    path: PtpPath = None
                    for p in host.meas_paths:
                        if datapath.id in p.path:
                            path = p
                            break

                    if path is not None:
                        out_port = self.get_out_port(datapath.id, path, True)

                        if out_port is not None:
                            actions.append(parser.OFPActionOutput(out_port))
                            match = parser.OFPMatch(in_port=in_port,
                                                    eth_type_nxm=PTP_ETH_TYPE,
                                                    ptp_msg_type=ptp_pkt.messageType.value,
                                                    ptp_src_clock_id=src_clockIdentity,
                                                    ptp_meas_type=ptp_pkt.msg.measType.value,
                                                    ptp_meas_target_clock_id=ptp_pkt.msg.targetClockIdentity)
                            dbg_paths.append(path.path)
                else:
                    # slave to master
                    host: PtpHost = self.ptp_hosts[src_clockIdentity]
                    path: PtpPath = None
                    for p in host.meas_paths:
                        if datapath.id in p.path:
                            path = p
                            break

                    if path is not None:
                        out_port = self.get_out_port(datapath.id, path, False)

                        if out_port is not None:
                            actions.append(parser.OFPActionOutput(out_port))
                            match = parser.OFPMatch(in_port=in_port,
                                                    eth_type_nxm=PTP_ETH_TYPE,
                                                    ptp_msg_type=ptp_pkt.messageType.value,
                                                    ptp_src_clock_id=src_clockIdentity,
                                                    ptp_meas_type=ptp_pkt.msg.measType.value)
                            dbg_paths.append(path.path)
            elif ptp_pkt.msg.measType == MeasurementType.MEAS_TRANSPORT:
                host: PtpHost = self.ptp_hosts.get(ptp_pkt.msg.targetClockIdentity, None)

                if host is None:
                    return

                path: PtpPath = None
                for p in host.meas_paths:
                    if datapath.id in p.path:
                        path = p
                        break

                if path is not None:
                    out_port = self.get_out_port(datapath.id, path, True)

                    if out_port is not None:
                        actions.append(parser.OFPActionOutput(out_port))
                        match = parser.OFPMatch(in_port=in_port,
                                                eth_type_nxm=PTP_ETH_TYPE,
                                                ptp_msg_type=ptp_pkt.messageType.value,
                                                ptp_src_clock_id=src_clockIdentity,
                                                ptp_meas_type=ptp_pkt.msg.measType.value,
                                                ptp_meas_target_clock_id=ptp_pkt.msg.targetClockIdentity)
                        dbg_paths.append(path.path)

        logger.debug(f"{actions=}")
        if not actions:
            return

        out = parser.OFPPacketOut(datapath=datapath,
                                  buffer_id=ofproto.OFP_NO_BUFFER,
                                  in_port=in_port,
                                  actions=actions,
                                  data=msg.data)
        datapath.send_msg(out)

        if match is not None:
            self.add_flow(datapath, 10, match, actions)

    def get_out_port(self, dpid: int, ptp_path: PtpPath, from_master: bool):
        inc = 1 if from_master else -1

        try:
            idx = ptp_path.path.index(dpid)
        except ValueError:
            return None

        # NOTE: TODO: check if inress port is connected to previous node on path
        next = ptp_path.path[idx + inc]

        if not self.clock_graph.has_edge(dpid, next):
            logger.error(f"NO EDGE {dpid} -- {next}\n{util.nx_to_graphviz(self.clock_graph)}")
            return None

        return self.clock_graph[dpid][next]['ports'][dpid]

    def _ptpsec_info_loop(self):
        INFO_LOOP_INTERVAL = 5
        while True:
            hub.sleep(INFO_LOOP_INTERVAL)
            # TODO: figure out correct condition when to continue
            self.topology_data.topology_change = False
            self.clock_graph = self.get_clock_graph()
            logger.info(f"Current clock graph:\n{util.nx_to_graphviz(self.clock_graph)}\n")

            if self.ptp_master is None:
                logger.warn("Warning: No known ptp master")
                continue

            if self.ptp_master not in self.clock_graph or self.master_main_port not in self.topology_data.graph:
                continue

            m_host = self.ptp_hosts[self.ptp_master]
            for ptp_host in self.ptp_hosts.values():
                ptp_host: PtpHost
                if ptp_host.clock_identity == self.ptp_master:
                    continue

                (paths, recommendations) = get_n_redundant_paths(self.clock_graph.copy(),
                                                                 m_host.clock_identity, ptp_host.clock_identity,
                                                                 REQUIRED_REDUNDANT_PATHS)
                logger.info(f"\n{paths=}\n{recommendations=}")
                if len(paths) < REQUIRED_REDUNDANT_PATHS:
                    logger.warn(
                        f"Unable to fulfill path requirements between master {
                            self.ptp_master} and slave {
                                ptp_host.clock_identity}.\nRecommended links to add: {recommendations}")

                ptp_host.paths = paths
                master_main_port_switch = list(self.topology_data.graph[self.master_main_port])[0]
                for path in paths:
                    if path[1] == master_main_port_switch:
                        ptp_host.main_path = PtpPath(path)
                        paths.remove(path)
                        ptp_host.meas_paths = [PtpPath(p)
                                               for p in paths][:REQUIRED_REDUNDANT_PATHS - 1]
                        break

    def get_clock_graph(self):
        mac_to_clockid = {}
        for host in self.ptp_hosts.values():
            host: PtpHost
            for mac in host.mac_to_portid.keys():
                mac_to_clockid[mac] = host.clock_identity
        return nx.relabel_nodes(self.topology_data.graph, mac_to_clockid, copy=True)

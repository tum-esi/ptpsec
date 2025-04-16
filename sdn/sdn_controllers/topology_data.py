from os_ken.base import app_manager
from os_ken.controller.handler import MAIN_DISPATCHER, set_ev_cls
from os_ken.ofproto import ofproto_v1_3
from os_ken.topology import event, switches
from os_ken.topology.api import get_all_host, get_all_switch, get_all_link
from os_ken.lib import hub

import networkx as nx

import util
import logging
logger = util.get_logger(__name__, logging.INFO, False)


class TopologyData(app_manager.OSKenApp):

    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    def __init__(self, *args, **kwargs):
        super(TopologyData, self).__init__(*args, **kwargs)
        self.name = 'topology_data'

        # graphs of the network
        self.graph: nx.DiGraph = nx.DiGraph()
        self.min_spanning_tree: nx.DiGraph = nx.DiGraph()

        self.topo_thread = hub.spawn(self._topo_loop)

        self.topology_change: bool = False
        self.topo_loop_uptime = 0

    def _topo_loop(self):
        UPDATE_TOPOLOGY_INTERVAL = 4
        while True:
            hub.sleep(UPDATE_TOPOLOGY_INTERVAL)
            logger.debug("automatically updating topology")
            self.update_topology()

            self.topo_loop_uptime += UPDATE_TOPOLOGY_INTERVAL

    @set_ev_cls([event.EventSwitchEnter,
                 event.EventSwitchLeave,
                 event.EventLinkAdd,
                 event.EventLinkDelete,
                 event.EventHostAdd,
                 event.EventHostDelete,
                 event.EventHostMove],
                MAIN_DISPATCHER)
    def topology_change_hanlder(self, ev):
        logger.info(type(ev))
        # self.update_topology()

    def update_topology(self):
        logger.debug("update_topology")
        switch_list: list[switches.Switch] = get_all_switch(self)
        links: switches.LinkState = get_all_link(self)
        hosts_list: list[switches.Host] = get_all_host(self)

        G, DG = self._get_graph(switch_list, hosts_list, links)
        self.graph = DG
        T = nx.minimum_spanning_tree(G)
        self.min_spanning_tree = T

        logger.debug("calling print_graphviz_graph")
        self.print_graphviz_graph(switch_list, links, hosts_list)

        logger.debug(f"Graph: \n{util.nx_to_graphviz(G)=}\n")
        logger.debug(f"Minimum spanning tree:\n{util.nx_to_graphviz(T)=}\n")

        self.topology_change = True

    def get_ip_graph(self) -> nx.DiGraph:
        mac_to_ip = {}
        for host in get_all_host(self):
            if host.ipv4:
                mac_to_ip[host.mac] = host.ipv4[0]
        return nx.relabel_nodes(self.graph, mac_to_ip, copy=True)

    def _get_graph(
        self, switch_list: list[switches.Switch], hosts_list: list[switches.Host], links: switches.LinkState
    ) -> nx.Graph:
        logger.debug("get_graph")
        DG = nx.DiGraph()
        G = nx.Graph()

        for switch in switch_list:
            G.add_node(switch.dp.id)
            DG.add_node(switch.dp.id, is_switch=True)

        for host in hosts_list:
            G.add_node(host.mac)
            G.add_edge(host.port.dpid, host.mac, ports={host.port.dpid: host.port.port_no})

            DG.add_node(host.mac, is_switch=False)
            DG.add_edge(host.port.dpid, host.mac, delay=1, ports={
                        host.port.dpid: host.port.port_no})
            DG.add_edge(host.mac, host.port.dpid, delay=1, ports={
                        host.port.dpid: host.port.port_no})

        used_links = set()
        for link in links.keys():
            src_id = link.src.dpid
            dst_id = link.dst.dpid

            delay = 1
            if self.graph.has_edge(src_id, dst_id):
                delay = self.graph[src_id][dst_id]['delay']
            DG.add_edge(src_id, dst_id, delay=delay,
                        ports={src_id: link.src.port_no, dst_id: link.dst.port_no})

            if (src_id, dst_id) in used_links or (dst_id, src_id) in used_links:
                continue

            G.add_edge(src_id, dst_id,
                       ports={src_id: link.src.port_no, dst_id: link.dst.port_no})
            used_links.add((src_id, dst_id))

        return G, DG

    def print_graphviz_graph(
        self, switch_list: list[switches.Switch], links: switches.LinkState, hosts_list: list[switches.Host]
    ) -> None:
        graph_str = "graph {"

        for switch in switch_list:
            graph_str += f'"{switch.dp.id}"[shape=diamond];'

        ip_to_hosts = {}
        for host in hosts_list:
            graph_str += f'"{host.mac}" -- "{
                host.port.dpid}" [taillabel="", headlabel="{host.port.port_no}"];'

            logger.debug(f'{host.mac} - {host.ipv4=}, {host.ipv6=}')
            if len(host.ipv4) < 1:
                logger.debug("host does not have an ipv4 address")
                graph_str += f'"{host.mac}"[shape=rectangle];'
                continue
            elif len(host.ipv4) > 1:
                logger.warning("host has multiple ipv4 addresses")

            ip = host.ipv4[0]

            if ip in ip_to_hosts:
                ip_to_hosts[ip].append(host)
            else:
                ip_to_hosts[ip] = [host]

        cluster_id = 0
        for ip in ip_to_hosts:
            graph_str += f"subgraph cluster_{cluster_id} {{"
            graph_str += f'label = "{ip}";'

            for host in ip_to_hosts[ip]:
                graph_str += f'"{host.mac}"[shape=rectangle];'

            graph_str += "};"
            cluster_id += 1

        used_links = []
        for link in links.keys():
            link: switches.Link
            src: switches.Port = link.src
            dst: switches.Port = link.dst
            if (dst, src) in used_links or (src, dst) in used_links:
                continue

            graph_str += f'"{src.dpid}" -- "{dst.dpid}" [taillabel="{
                src.port_no}", headlabel="{dst.port_no}"];'
            used_links.append((src, dst))

        graph_str += "}"

        logger.info(f"Current topology:\n{graph_str}\n")

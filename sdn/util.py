import logging
import networkx as nx


def is_multicast(mac: str) -> bool:
    if mac is None:
        return False

    first_octet = int(mac.split(':')[0], 16)

    return (first_octet & 1) == 1

def get_logger(name: str, level: int, log_to_file: bool = False) -> logging.Logger:
    logger = logging.getLogger(name)
    logger.propagate = False
    logger.setLevel(level)

    formatter = logging.Formatter('[%(levelname)s - %(name)s]: %(message)s')

    ch = logging.StreamHandler()
    ch.setFormatter(formatter)
    logger.addHandler(ch)

    if log_to_file:
        fh = logging.FileHandler(f"{name}.log")
        fh.setFormatter(formatter)
        logger.addHandler(fh)

    return logger


logger = get_logger(__name__, logging.INFO)


def nx_to_graphviz(G: nx.Graph) -> str:
    s = "graph {"
    for (u, v) in G.edges():
        s += f'"{u}" -- "{v}";'
    s += "}"
    return s

def get_n_redundant_paths(G: nx.DiGraph, s, t, n: int) -> (list, list):
    try:
        paths = list(nx.node_disjoint_paths(G, s, t))
    except Exception as e:
        logger.critical(e)
        return [], []

    if len(paths) >= n:
        return paths, []

    for path in paths:
        G.remove_nodes_from(path[1:-1])

        # delete edge between s and t if they are directly connected
        if len(path) == 2:
            if (s, t) in G.edges:
                G.remove_edge(s, t)
            if (t, s) in G.edges:
                G.remove_edge(t, s)

    recommended_edges = []
    while len(paths) + len(recommended_edges) < n:
        s_reach = nx.single_source_dijkstra_path_length(G, s, weight='delay')
        t_reach = nx.single_source_dijkstra_path_length(G, t, weight='delay')

        # search min reachable node
        # TODO: this can be optimized, because the min reachable node is always the nearest neighbor
        min_s_reach = None
        for k in s_reach:
            if k != s and (min_s_reach is None or s_reach[k] < s_reach[min_s_reach]):
                min_s_reach = k

        if not min_s_reach:
            break

        min_t_reach = None
        for k in t_reach:
            if k != t and (min_t_reach is None or t_reach[k] < s_reach[min_t_reach]):
                min_t_reach = k

        if not min_t_reach:
            break

        recommended_edges.append((min_s_reach, min_t_reach))

        G.remove_nodes_from(nx.single_source_dijkstra(G, s, target=min_s_reach)[1:-1])
        G.remove_nodes_from(nx.single_source_dijkstra(G, t, target=min_t_reach)[1:-1])

    return (paths, recommended_edges)


#    4  5
#   /   |
#  /    |
# 1-----3
#  \   /
#   \ /
#    2
def ex_graph_1() -> nx.Graph:
    G = nx.Graph()

    G.add_edge(1, 2)
    G.add_edge(1, 3)
    G.add_edge(1, 4)
    G.add_edge(2, 3)
    G.add_edge(3, 5)

    return G


if __name__ == '__main__':
    G = ex_graph_1()
    print(nx_to_graphviz(G))

    N = 3
    x = get_n_redundant_paths(G, 1, 3, N)
    print(x)

from os_ken.lib import stringify

# TODO: both classes can be simplified because we now use PTP on L2 instead of UDP

class PtpPath(stringify.StringifyMixin):
    def __init__(self, path: list):
        self.path = path
        # ipv4 identification number to next index in path and how the index changes (1 or -1)
        self.id_to_idx: dict[int, (int, int)] = {}

class PtpHost(stringify.StringifyMixin):
    def __init__(self, clockIdentity: int):
        self.clock_identity = clockIdentity
        self.mac_to_portid = {}
        self.meas_paths: list[PtpPath] = []
        self.main_path: PtpPath = None

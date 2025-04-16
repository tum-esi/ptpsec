from os_ken.base import app_manager

from sdn_controllers.topology_data import TopologyData
from sdn_controllers.delay_monitor import DelayMonitor
from sdn_controllers.regular_switch import RegularSwitch
from sdn_controllers.ptpsec_controller import PTPSecController

class PTPSecApp(app_manager.OSKenApp):
    _CONTEXTS = {
        'topology_data': TopologyData,
        'delay_monitor': DelayMonitor,
        'regular_switch': RegularSwitch,
        'ptpsec_controller': PTPSecController,
    }

    def __init__(self, *args, **kwargs):
        super(PTPSecApp, self).__init__(*args, **kwargs)
        self.name = 'ptpsec_app'

        # External Apps - Load order: start TopologyData first since the other apps depend on it:
        self.topology_data: TopologyData = kwargs['topology_data']
        self.delay_monitor: DelayMonitor = kwargs['delay_monitor']
        self.regular_switch: RegularSwitch = kwargs['regular_switch']
        self.ptpsec_controller: PTPSecController = kwargs['ptpsec_controller']

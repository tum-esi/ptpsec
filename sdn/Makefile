run-controller:
	. .venv/bin/activate && \
	osken-manager ptpsec_app.py --observe-links

run-controller-v:
	. .venv/bin/activate && \
	osken-manager ptpsec_app.py --verbose --observe-links

start-services:
	systemctl start ovsdb-server.service ovs-vswitchd.service

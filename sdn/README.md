# Structure
- The main application is `ptpsec_app.py`
- `ptp` contains ptp specific classes such as each message type
- `sdn_controllers` contains the different components of the main app
- `topologies.py` starts mininet with different topologies

# Execution
Create a virtual Python environment
```sh
python -m venv venv
source venv/bin/activate
```
Install the required dependencies with
```sh
pip install -r requirements.txt
```
Run mininet with a topology of choice (requires root privileges):
```sh
# See topologies.py for code of the topologies
./topologies.py <topology>
```
This opens a terminal for each host and the controller.
To start the controller run the following command in the controller terminal:
```sh
make run-controller
```
In the host terminals execute `slave.sh` or `master.sh` to run ptpsec as slave or master respectively.
This requires that the ptpsec executable is in the current directory (e.g. via a symlink).


## Troubleshooting
To run mininet, the required systemd services need to run. You can start them with:
```sh
make start-services
```

## Configuration
- You can control the required amount of redundant paths in `settings.py`
- You can adapt the logging behavior (level and whether or not to save to a file) of the different components at the top of the respective files (e.g. line 11 of `sdn_controllers/topology_data.py`).

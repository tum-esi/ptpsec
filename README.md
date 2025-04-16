# PTPsec
PTPsec: Securing the Precision Time Protocol Against Time Delay Attacks Using Cyclic Path Asymmetry Analysis


## General Information
This repository contains the source code of our PTPsec implementation and the MitM attacker node. 

The PTPsec implementation is based on linuxptp-3.1.1 (https://github.com/richardcochran/linuxptp/releases/tag/v3.1.1). <br>
The attacker node was developed as DPDK application (https://github.com/DPDK/dpdk). 

For further information refer to the paper or contact the authors.


## Installation & Setup

### PTPsec
PTPsec follows the build steps of linuxptp. The default setup for a detection clock requires two NICs with HWTS support. To internally synchronize the HW clocks, the ts2phc program can be used with our custom profiles for master and slave, respectively:

```
./ts2phc -f ./configs/ptpsec_ts2phc_master.cfg
```

The PTPsec program can be started as follows:

```
./ptp4l -f ./configs/ptpsec_ptp4l_master.cfg
```



### Attacker
The attacker node is a custom DPDK application that uses the meson build system. For further details refer to the examples of the official DPDK repository (https://github.com/DPDK/dpdk).
The attacker node requires two NICs with HWTS support. In our case, we used Intel i210 NICs for which we developed additional driver code to support the HWTS feature in DPDK. 
To successfully use the NICs in DPDK, dedicated memory (hugepages) must be allocated and the DPDK driver be loaded in advance (refer to DPDK for further details).

## Citation
If you use this work, please cite our papers:

Conference Paper (2024):
```
@inproceedings{finkenzeller2024ptpsec,
  title        = { PTPsec: Securing the Precision Time Protocol Against Time Delay Attacks Using Cyclic Path Asymmetry Analysis },
  author       = { Andreas Finkenzeller and Oliver Butowski and Emanuel Regnath and Mohammad Hamad and Sebastian Steinhorst },
  year         = { 2024 },
  organization = { IEEE },
  booktitle    = { IEEE International Conference on Computer Communications (IEEE INFOCOM 2024) },
  pages        = { 461--470 },
  location     = { Vancouver, Canada },
  doi          = { 10.1109/INFOCOM52122.2024.10621345 },
  url          = { https://tum-esi.github.io/publications-list/PDF/2024-INFOCOM-PTPsec.pdf },
}
```

Extended Journal Article (2025):
```
@article{finkenzeller2025securing,
    title     = {Securing the Precision Time Protocol with SDN-enabled Cyclic Path Asymmetry Analysis},
    author    = {Andreas Finkenzeller and Arne Fucks and Emanuel Regnath and Mohammad Hamad and Sebastian Steinhorst},
    year      = {2025},
    journal   = {ACM Transactions on Cyber-Physical Systems},
    publisher = {Association for Computing Machinery},
    address   = {New York, NY, USA},
    pages     = {25},
    doi       = {https://doi.org/10.1145/3728363},
    ISSN      = {2378-962X},
    url       = {https://tum-esi.github.io/publications-list/PDF/2025-ACM-TCPS-PTPsec.pdf},
}
```


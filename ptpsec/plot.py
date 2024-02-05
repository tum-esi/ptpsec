# Imports
import numpy as np
import scipy.signal
import matplotlib.pyplot as plt

# Data arrays
time = []
asym = []
rt_m = []
rt_s = []
t1 = []
t2 = []
tm1 = []
tm2 = []
offset = []
delay = []

# Open measurements file
master = False
skip = 10
if master:
    with open('./measurements_master.txt', 'r') as file:
        lines = file.readlines()[skip+2:]
        for line in lines:
            m = line.split(',')
            time.append(int(m[0]))
            rt_m.append(int(m[1]))
            t1.append(int(m[2]))
            t2.append(int(m[3]))
            tm1.append(int(m[4]))
            tm2.append(int(m[5]))
else:
    with open('./measurements.txt', 'r') as file:
        lines = file.readlines()[skip+2:]
        for line in lines:
            m = line.split(',')
            time.append(int(m[0]))
            asym.append(int(m[1]))
            rt_m.append(int(m[2]))
            rt_s.append(int(m[3]))
            t1.append(int(m[4]))
            t2.append(int(m[5]))
            tm1.append(int(m[6]))
            tm2.append(int(m[7]))
            offset.append(int(m[8]))
            delay.append(int(m[9]))

# Convert to numpy array
time = np.array(time)
asym = np.array(asym)
rt_m = np.array(rt_m)
rt_s = np.array(rt_s)
t1 = np.array(t1)
t2 = np.array(t2)
tm1 = np.array(tm1)
tm2 = np.array(tm2)
offset = np.array(offset)
delay = np.array(delay)

# Limit axis
# if x_min is not None and x_max is not None:
#     x_axis = x_axis[x_min:-x_max]
#     points = points[x_min:-x_max]
# if x_min is not None:
#     x_axis = x_axis[x_min:]
#     points = points[x_min:]

# Scale data
asym = asym / 10**3         # ns -> us
rt_m = rt_m / 10**3         # ns -> us
rt_s = rt_s / 10**3         # ns -> us
t1 = t1 % 10**12            # Only keep interesting part (~1000s)
t1 = t1 / 10**3             # ns -> us
t2 = t2 % 10**12            # Only keep interesting part (~1000s)
t2 = t2 / 10**3             # ns -> us
tm1 = tm1 % 10**12          # Only keep interesting part (~1000s)
tm1 = tm1 / 10**3           # ns -> us
tm2 = tm2 % 10**12          # Only keep interesting part (~1000s)
tm2 = tm2 / 10**3           # ns -> us
offset = offset / 10**3     # ns -> us
delay = delay / 10**3       # ns -> us

# Plot data
if master:
    plt.plot(time, rt_m, label="RT Master")
    plt.show()
    key = input('Press any key to exit...')

    # Create figure
    plot_h = 3
    plot_w = 2
    fig, ax = plt.subplots(plot_h, plot_w)

    data = [rt_m, t1, t2, tm1, tm2]
    labels = ["RT Master", "t1", "t2", "tm1", "tm2"]
    # y_labels = ["Measured asymmetry in $\mu s$", "PTP Offset $\mu s$", "PTP Link Delay $\mu s$"]
    for i in range(5):
        ax[i//plot_w, i%plot_w].plot(time, data[i], label=labels[i])
        # ax[i].set_ylabel(y_labels[i])
        ax[i//plot_w, i%plot_w].set_xlabel("Measurement Id")
        ax[i//plot_w, i%plot_w].grid(True, 'major', 'y')
        ax[i//plot_w, i%plot_w].legend()
        # ax[i].set_title(title)
else:
    # plt.plot(time, t1, 'r', label="t1")
    # plt.plot(time, t2, 'g', label="t2")
    # plt.plot(time, tm1, 'b', label="tm1")
    # plt.plot(time, tm2, 'y', label="tm2")
    # plt.legend()
    # plt.show()
#    key = input('Press any key to exit...')

    # Create figure
    plot_h = 3
    plot_w = 3
    fig, ax = plt.subplots(plot_h, plot_w)

    data = [asym, rt_m, rt_s, t1, t2, tm1, tm2, offset, delay]
    labels = ["Asymmetry", "RT Master", "RT Slave", "t1", "t2", "tm1", "tm2", "Offset", "Delay"]
    # y_labels = ["Measured asymmetry in $\mu s$", "PTP Offset $\mu s$", "PTP Link Delay $\mu s$"]
    for i in range(9):
        ax[i//plot_w, i%plot_w].plot(time, data[i], label=labels[i])
        # ax[i].set_ylabel(y_labels[i])
        ax[i//plot_w, i%plot_w].set_xlabel("Measurement Id")
        ax[i//plot_w, i%plot_w].grid(True, 'major', 'y')
        ax[i//plot_w, i%plot_w].legend()
        # ax[i].set_title(title)

# fig.suptitle('PTP Measurements', fontsize=30)
fig.tight_layout(pad=0)
fig.subplots_adjust(left=0.05, bottom=0.05, right=0.95, top=0.95, wspace=0.25, hspace=0.45)
fig.show()

key = input('Press any key to exit...')

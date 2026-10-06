# hpDIRC analysis kit

Everything needed to analyse hpDIRC test-stand data on your own laptop, inside VS Code,
with Jupyter notebooks. ROOT and the DAQ's Event library are installed for you in a
container, so you don't need Linux or ROOT experience. You write Python with numpy,
pandas and matplotlib.

## One-time setup (about 15 minutes, mostly downloading)

1. Install **VS Code**: https://code.visualstudio.com
2. Install **Docker Desktop**: https://www.docker.com/products/docker-desktop and start it.
   * Windows: accept the WSL 2 option when it asks.
   * Mac with Apple silicon (M1/M2/M3/...): in Docker Desktop, *Settings > General*, tick
     **"Use Rosetta for x86_64/amd64 emulation"**. The environment runs under emulation,
     so it is slower than on an Intel/AMD machine.
3. In VS Code, install the **Dev Containers** extension (Extensions panel, search "Dev Containers").
4. Put the data files in the `data/` folder of this kit (see below).
5. *File > Open Folder...* and pick this folder. VS Code asks **"Reopen in Container"**: click it.
   (Or press F1 and run *Dev Containers: Reopen in Container*.)
   The first time takes ~10 minutes while it builds; after that it opens in seconds.
   (if you still can't see it, from the target folder run:
   `docker build --file .devcontainer\Dockerfile --tag hpdirc-analysis:devcontainer .`)
6. Open `starter.ipynb`. If VS Code asks for a kernel, pick **Python 3 (/usr/bin/python3)**.
   Run the cells with Shift+Enter.

## Data

Put these in `data/`:

* the run files, e.g. `test-00000068-0000.evt`
* the digitiser calibration `calib_0049_5G.dat` (already included)

The `0049` is the serial number of the V1742 digitiser. Each run records the serial it was taken
with as a "packet" (`run.values(921)`), and the matching calibrations are kept on the DAQ machine in
`/scratch2/data/v1742.db/`.

Files are big (hundreds of MB to several GB). Start with `N_EVENTS = 1000` in the notebook
before reading a whole file.

## Using the `hpdirc` module

```python
import hpdirc
run = hpdirc.Run("data/test-00000068-0000.evt", calib="data/calib_0049_5G.dat")

# access other packet information
print(run.hv())        # HV settings/readback at the start of the run
print(run.motor())     # motor x/y position

for ev in run.events(max_events=1000):
    ev.wave       # (32, 1024) waveform in ADC counts, calibrated
    ev.time       # (32, 1024) time of each sample in ns
    ev.trig       # (4, 1024)  trigger signal of each group of 8 channels
    ev.trigtime   # (4, 1024)
    ev.number     # event number
```

What it does for you:

* applies the DRS4 calibration (per-cell offsets and the per-cell time axis);
* removes the DRS4 "spikes": 1-2 sample glitches that show up at the same sample in the
  channels of one group (the same algorithm as the `glasgow` pmonitor code on the DAQ
  machine). `hpdirc.Run(..., despike=False)` turns this off if you want to see its effect;
* stores the begin-run information (setup script, digitiser info, motor, HV), which you can
  read with `run.text(<packet id>)`.

## Other datamanagement info
* `prdfsplit` (in the container, and on the DAQ machine) can cut a large `.evt` file into
  smaller pieces.

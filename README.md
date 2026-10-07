# hpDIRC analysis kit

Everything needed to analyse hpDIRC test-stand data on your own laptop, inside VS Code,
with Jupyter notebooks. ROOT and the DAQ's Event library are installed for you in a
container, so you don't need Linux or ROOT experience. You write Python with numpy,
pandas and matplotlib.

## One-time setup (about 15 minutes, mostly downloading)

1. Install **VS Code**: https://code.visualstudio.com
2. Install **Docker Desktop**: https://www.docker.com/products/docker-desktop and start it.
   * Windows: accept the WSL 2 option when it asks. Enable Ubuntu under Docker Desktop > Settings > Resources > WSL Integration.
     If Docker later says *permission denied*, your Windows user must be in the **docker-users**
     group: run `net localgroup docker-users <your-windows-username> /add` in an
     *Administrator* PowerShell, then sign out of Windows and back in.
   * Mac with Apple silicon (M1/M2/M3/...): When prompted, use **"Use Rosetta for x86_64/amd64 emulation"**.
3. In VS Code, install the **Dev Containers** extension (Extensions panel, search "Dev Containers").
4. Fork your own version of this github and clone it into an ordinary folder (e.g. `Documents`,
   not OneDrive or `Program Files`) (Can be done in vscode, or via `git clone https://github.com/<github-username>/mcp-project-analysis.git` ) 
5. *File > Open Folder...* and pick this folder. VS Code will ask **"Reopen in Container"**: click it.
   (Or press F1 and run *Dev Containers: Reopen in Container*.)
   The first time takes ~10 minutes while it builds; after that it opens in seconds.
6. Open `starter.ipynb`. If VS Code asks for a kernel (top right of the notebook, *Select Kernel*),
   choose *Jupyter Kernel...* and then **hpDIRC (Python 3 + ROOT)**. If that is not listed,
   *Python Environments...* > **/usr/bin/python3** also works.

**If something goes wrong:**

* Try closing and reopening Docker Desktop and VS Code first.
* After pulling updates to `.devcontainer/`, rebuild: F1 > *Dev Containers: Rebuild Container*.
* To check the environment, open a terminal in VS Code (*Terminal > New Terminal*) and run
  `python3 -c "import hpdirc; hpdirc._setup(); print('OK')"`. If that prints OK, the
  installation is fine and any remaining problem is in VS Code's kernel selection.

## Data

`data/` already contains a small example run (`test-00000068-0000.evt`, 727 events) and the
digitiser calibration `calib_0049_5G.dat`. Put any other run files you are given there too.

The `0049` is the serial number of the V1742 digitiser. Each run records the serial it was taken
with as a "packet" (`run.values(921)`), and the matching calibrations are kept on the DAQ machine in
`/scratch2/data/v1742.db/`.

Files are big (hundreds of MB to several GB). Start with `N_EVENTS = 5000` (the notebook default)
before reading a whole file. It might be wise to not run in a notebook once you have your code developed.

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

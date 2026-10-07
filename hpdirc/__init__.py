"""
hpdirc - read hpDIRC test-beam data (RCDAQ .evt files, CAEN V1742 digitizer) into numpy.

    import hpdirc
    run = hpdirc.Run("data/test-00000067-0000.evt", calib="data/calib_0049_5G.dat")
    for ev in run.events(max_events=100):
        ev.wave      # (32, samples) ADC counts, DRS4-corrected
        ev.time      # (32, samples) ns, per-cell calibrated time axis
        ev.trig      # (4, samples) fast-trigger (TR0/TR1) waveform per group
        ev.trigtime  # (4, samples) ns

    print(run.hv())      # HV readback stored at the start of the run
    print(run.motor())   # motor x/y stored at the start of the run

By default the DRS4 "spikes" are removed: 1-2 sample glitches that appear at the same
sample in the channels of one group (8 signals + trigger), an artefact of the chip
(CAEN calls the fix PeakCorrection). Same algorithm as the glasgow pmonitor code.
Pass despike=False to Run(...) to see the data without it.

Under the hood this uses ROOT (PyROOT) and the sPHENIX online_distribution Event
library; the heavy lifting is in hpdirc_reader.h.
"""

import os
from dataclasses import dataclass

import numpy as np

_loaded = False


def _setup():
    global _loaded
    if _loaded:
        return
    import ROOT

    online = os.environ.get("ONLINE_MAIN", "")
    if online:
        ROOT.gInterpreter.AddIncludePath(os.path.join(online, "include", "Event"))
        ROOT.gSystem.AddDynamicPath(os.path.join(online, "lib"))
    if ROOT.gSystem.Load("libEvent") < 0:
        raise RuntimeError("could not load libEvent - is online_distribution installed and ONLINE_MAIN set?")
    here = os.path.dirname(os.path.abspath(__file__))
    if not ROOT.gInterpreter.Declare(f'#include "{os.path.join(here, "hpdirc_reader.h")}"'):
        raise RuntimeError("could not compile hpdirc_reader.h")
    _loaded = True


# begin-run packet ids written by hpDIRC_setup.sh
SETUP_SCRIPT = 900
V1742_INFO = 920
V1742_SERIAL = 921
MOTOR_INFO = 930
MOTOR_XY = 931
HV_INFO = 940
HV_VALUES = 941


@dataclass
class DigitizerEvent:
    run: int
    number: int
    unixtime: int
    wave: np.ndarray
    time: np.ndarray
    trig: np.ndarray
    trigtime: np.ndarray
    index_cell: tuple
    group_trigger_time: tuple

    @property
    def samples(self):
        return self.wave.shape[1]


class Run:
    """One .evt file. Iterate over it with run.events()."""

    def __init__(self, filename, calib=None, packet=2000, despike=True):
        _setup()
        import ROOT

        if not os.path.exists(filename):
            raise FileNotFoundError(filename)
        if calib is not None and not os.path.exists(calib):
            raise FileNotFoundError(calib)
        self.filename = filename
        self.calib = calib
        self._packet = packet
        self._despike = despike
        self._reader = ROOT.HpdircReader(filename, calib or "", packet)
        if not self._reader.ok():
            raise IOError(f"could not open {filename}")
        if calib is None:
            print("hpdirc: no calibration file given - waveforms are RAW, time axis is nominal")
        self._first = None
        # read up to the first data event so begin-run info is available straight away
        if self._reader.next():
            self._first = self._grab()

    # -------- events --------
    def _grab(self):
        r = self._reader
        ns = r.samples()
        wave = np.empty((32, ns), dtype=np.float32)
        time = np.empty((32, ns), dtype=np.float32)
        trig = np.empty((4, ns), dtype=np.float32)
        trigtime = np.empty((4, ns), dtype=np.float32)
        r.fill(wave, time, trig, trigtime, self._despike)
        return DigitizerEvent(
            run=r.run(),
            number=r.event(),
            unixtime=r.unixtime(),
            wave=wave,
            time=time,
            trig=trig,
            trigtime=trigtime,
            index_cell=tuple(r.index_cell(g) for g in range(4)),
            group_trigger_time=tuple(int(r.group_trigger_time(g)) for g in range(4)),
        )

    def events(self, max_events=None):
        """Yield DigitizerEvent objects. A Run can only be read through once;
        make a new Run(...) to start again from the beginning."""
        n = 0
        if self._first is not None:
            ev, self._first = self._first, None
            yield ev
            n += 1
        while max_events is None or n < max_events:
            if not self._reader.next():
                return
            yield self._grab()
            n += 1

    # -------- begin-run information --------
    def beginrun_packets(self):
        return list(self._reader.beginrun_packets())

    def text(self, packet_id):
        """Text dump of a begin-run packet (as `ddump -t 9 -p <id>` would show)."""
        return str(self._reader.text(packet_id))

    def values(self, packet_id):
        return list(self._reader.values(packet_id))

    def hv(self):
        return self.text(HV_INFO)

    def motor(self):
        return self.text(MOTOR_INFO)

    def digitizer(self):
        return self.text(V1742_INFO)

    def setup_script(self):
        return self.text(SETUP_SCRIPT)

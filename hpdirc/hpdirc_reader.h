// hpdirc_reader.h - small C++ wrapper around the online_distribution Event library
// for the hpDIRC CAEN V1742 data. It is compiled on the fly by ROOT (cling) when
// hpdirc.py is imported; students normally only use the Python side.
//
// Owns the iterator, the current Event and Packet, and the DRS4 correction, and
// copies a whole event into caller-supplied float buffers in one call so that
// Python never has to loop over 32 x 1024 samples itself.

#include <fileEventiterator.h>
#include <Event.h>
#include <packet.h>
#include <caen_correction.h>

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

class HpdircReader {
public:
  HpdircReader(const char *filename, const char *calibfile, int packetid = 2000)
    : _packetid(packetid)
  {
    int status = 0;
    _it = new fileEventiterator(filename, status);
    _ok = (status == 0);
    if (calibfile && calibfile[0]) {
      std::ifstream test(calibfile);
      if (test.good()) { _cc = new caen_correction(calibfile); read_nsample(calibfile); }
    }
  }

  ~HpdircReader() { clear(); delete _cc; delete _it; }

  bool ok() const { return _ok; }
  bool calibrated() const { return _cc != nullptr; }

  // Advance to the next data event that contains the digitizer packet.
  // Begin-run (type 9) events met on the way are stored, see text() / values().
  bool next()
  {
    while (true) {
      clear();
      _evt = _it->getNextEvent();
      if (!_evt) return false;
      if (_evt->getEvtType() == 9) { store_beginrun(_evt); continue; }
      if (_evt->getEvtType() != 1) continue;
      _pkt = _evt->getPacket(_packetid);
      if (!_pkt) continue;
      if (_cc) _cc->init(_pkt);
      return true;
    }
  }

  int run() const { return _evt ? _evt->getRunNumber() : -1; }
  int event() const { return _evt ? _evt->getEvtSequence() : -1; }
  long unixtime() const { return _evt ? (long) _evt->getTime() : 0; }
  int samples() const { return _pkt ? _pkt->iValue(0, "SAMPLES") : 0; }
  int channels() const { return _pkt ? _pkt->iValue(0, "CHANNELS") : 0; }
  int frequency_code() const { return _pkt ? _pkt->iValue(0, "FREQUENCY") : -1; }
  int group_present(int g) const { return _pkt ? _pkt->iValue(g, "GROUPPRESENT") : 0; }
  int index_cell(int g) const { return _pkt ? _pkt->iValue(g, "INDEXCELL") : 0; }
  unsigned long long group_trigger_time(int g) const
  { return _pkt ? (unsigned long long) _pkt->lValue(g, "GROUPTRIGGERTIME") : 0; }

  // nominal sample period in ns from the FREQUENCY code (0 = 5, 1 = 2.5, 2 = 1 GS/s)
  float nominal_period() const
  {
    switch (frequency_code()) {
      case 0: return 0.2f;
      case 1: return 0.4f;
      case 2: return 1.0f;
      default: return 0.2f;
    }
  }

  // wave[32*ns], time[32*ns], trig[4*ns], trigtime[4*ns]  (row-major: channel, sample)
  // With a calibration file: DRS4 cell and sample-position offsets subtracted, per-cell time axis in ns.
  // Without: raw ADC counts and a nominal, evenly spaced time axis.
  int fill(float *wave, float *time, float *trig, float *trigtime,
           bool despike = true, float despike_threshold = 40.96f, int despike_min_channels = 5) const
  {
    if (!_pkt) return 0;
    const int ns = samples();
    const float dt = nominal_period();
    for (int ch = 0; ch < 32; ch++) {
      for (int s = 0; s < ns; s++) {
        const int k = ch * ns + s;
        if (_cc) { wave[k] = _cc->caen_corrected(s, ch) - nsample(ch / 8, s, ch % 8); time[k] = _cc->caen_time(s, ch); }
        else     { wave[k] = _pkt->iValue(s, ch);        time[k] = s * dt; }
      }
    }
    for (int g = 0; g < 4; g++) {
      for (int s = 0; s < ns; s++) {
        const int k = g * ns + s;
        if (_cc) { trig[k] = _cc->caen_corrected(s, 32 + g) - nsample(g, s, 8); trigtime[k] = _cc->caen_time(s, 32 + g); }
        else     { trig[k] = _pkt->iValue(s, 32 + g);        trigtime[k] = s * dt; }
      }
    }
    // sample 0 of the DRS4 readout is unreliable; CAEN copies sample 1 over it
    for (int ch = 0; ch < 32; ch++) wave[ch * ns] = wave[ch * ns + 1];
    for (int g = 0; g < 4; g++)     trig[g * ns] = trig[g * ns + 1];
    if (despike) remove_spikes(wave, trig, ns, despike_threshold, despike_min_channels);
    return ns;
  }

  // DRS4 spike removal - same algorithm as remove_spikes() in rcdaq_scripts/glasgow/glasgow.cc
  // (there: 10 mV = 40.96 ADC counts, 5 of 9 channels). A bad DRS4 cell shows up as a 1-2
  // sample spike at the same sample on all 9 channels of a chip (8 signals + trigger); a real
  // pulse is confined to one channel. Flagged samples are replaced by the neighbours' average
  // (1-sample spikes) or a linear ramp between the flanking samples (2-sample spikes).
  static void remove_spikes(float *wave, float *trig, int ns, float thr, int min_ch)
  {
    for (int g = 0; g < 4; g++) {
      float *v[9];
      for (int c = 0; c < 8; c++) v[c] = wave + (g * 8 + c) * ns;
      v[8] = trig + g * ns;

      for (int s = 1; s < ns - 1; s++) {
        int count = 0;
        for (int k = 0; k < 9; k++) {
          const float dp = v[k][s] - v[k][s - 1];
          const float dm = v[k][s] - v[k][s + 1];
          if (dp * dm > 0 && std::fabs(dp) > thr && std::fabs(dm) > thr) count++;
        }
        if (count >= min_ch)
          for (int k = 0; k < 9; k++) v[k][s] = 0.5f * (v[k][s - 1] + v[k][s + 1]);
      }

      for (int s = 1; s < ns - 2; s++) {
        int count = 0;
        for (int k = 0; k < 9; k++) {
          const float mid = 0.5f * (v[k][s] + v[k][s + 1]);
          const float dp = mid - v[k][s - 1];
          const float dm = mid - v[k][s + 2];
          if (dp * dm > 0 && std::fabs(dp) > thr && std::fabs(dm) > thr) count++;
        }
        if (count >= min_ch)
          for (int k = 0; k < 9; k++) {
            const float lo = v[k][s - 1], hi = v[k][s + 2];
            v[k][s]     = lo + (hi - lo) / 3.0f;
            v[k][s + 1] = lo + 2.0f * (hi - lo) / 3.0f;
          }
      }
    }
  }

  // begin-run information (setup script, digitizer info, motor position, HV, ...)
  std::vector<int> beginrun_packets() const
  {
    std::vector<int> ids;
    for (auto &kv : _text) ids.push_back(kv.first);
    return ids;
  }
  std::string text(int id) const
  {
    auto f = _text.find(id);
    return f == _text.end() ? std::string() : f->second;
  }
  std::vector<int> values(int id) const
  {
    auto f = _values.find(id);
    return f == _values.end() ? std::vector<int>() : f->second;
  }

private:
  // CAEN's second correction (wavedump CORRECTION_LEVEL bit 1): 
  // a pedestal per sample *position* in the readout window. 
  // The caen_correction reads these columns but discards them, so we read them here. 
  // File layout per chip: 1024 lines of index  cell[0..8]  time  nsample[0..8]
  void read_nsample(const char *calibfile)
  {
    std::ifstream in(calibfile);
    _nsample.assign(4 * 1024 * 9, 0.f);
    for (int chip = 0; chip < 4; chip++)
      for (int i = 0; i < 1024; i++) {
        int index; int cell; float t; int ns;
        if (!(in >> index)) { _nsample.assign(4 * 1024 * 9, 0.f); return; }
        for (int c = 0; c < 9; c++) in >> cell;
        in >> t;
        for (int c = 0; c < 9; c++) { in >> ns; _nsample[(chip * 1024 + i) * 9 + c] = ns; }
      }
  }
  float nsample(int chip, int s, int c) const
  {
    return (_nsample.empty() || s < 0 || s >= 1024) ? 0.f : _nsample[(chip * 1024 + s) * 9 + c];
  }

  void clear()
  {
    delete _pkt; _pkt = nullptr;
    delete _evt; _evt = nullptr;
  }

  void store_beginrun(Event *e)
  {
    Packet *list[1000];
    const int n = e->getPacketList(list, 1000);
    for (int i = 0; i < n; i++) {
      const int id = list[i]->getIdentifier();
      std::ostringstream os;
      list[i]->dump(os);
      _text[id] = os.str();
      std::vector<int> v;
      const int len = list[i]->getDataLength();
      for (int j = 0; j < len; j++) v.push_back(list[i]->iValue(j));
      _values[id] = v;
      delete list[i];
    }
  }

  int _packetid;
  bool _ok = false;
  Eventiterator *_it = nullptr;
  Event *_evt = nullptr;
  Packet *_pkt = nullptr;
  caen_correction *_cc = nullptr;
  std::vector<float> _nsample;
  std::map<int, std::string> _text;
  std::map<int, std::vector<int>> _values;
};

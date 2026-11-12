// Photon - versioned save and load of the complete solver state (R14).
//
// File layout (little endian, uncompressed for now):
//   "PHOTONST" magic, u32 format version, profile name, grid (nx ny nz dx origin),
//   a fingerprint of the discretized scene (barrier count, fuel cell count,
//   ignition-free hash of the geometry), then every field in a fixed order.
// A state only loads into a solver built from the same scene and profile;
// anything else is refused with a message instead of being loaded wrongly.
#include "sim/solver.h"
#include <cstring>
#include <fstream>

namespace photon {

namespace {
const char MAGIC[8] = {'P', 'H', 'O', 'T', 'O', 'N', 'S', 'T'};

struct Out {
    std::ofstream f;
    template <class T> void pod(const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
    void str(const std::string& s) { uint32_t n = (uint32_t)s.size(); pod(n); f.write(s.data(), n); }
    template <class T> void vec(const std::vector<T>& v) {
        uint64_t n = v.size();
        pod(n);
        if (n) f.write(reinterpret_cast<const char*>(v.data()), (std::streamsize)(n * sizeof(T)));
    }
};
struct In {
    std::ifstream f;
    bool ok = true;
    template <class T> void pod(T& v) { if (ok && !f.read(reinterpret_cast<char*>(&v), sizeof(T))) ok = false; }
    void str(std::string& s) {
        uint32_t n = 0;
        pod(n);
        if (!ok || n > (1u << 20)) { ok = false; return; }
        s.resize(n);
        if (n && !f.read(&s[0], n)) ok = false;
    }
    template <class T> void vec(std::vector<T>& v, size_t expect = (size_t)-1) {
        uint64_t n = 0;
        pod(n);
        if (!ok || (expect != (size_t)-1 && n != expect) || n > (1ull << 32)) { ok = false; return; }
        v.resize(n);
        if (n && !f.read(reinterpret_cast<char*>(v.data()), (std::streamsize)(n * sizeof(T)))) ok = false;
    }
};

// Geometry fingerprint: what the discretizer produced from the scene.
uint64_t fingerprint(const Solver& s) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix((uint64_t)s.N);
    mix(s.barriers.size());
    for (const Barrier& b : s.barriers) { mix((uint64_t)b.face); mix(b.axis); mix((uint64_t)b.assembly); }
    for (int c = 0; c < s.N; ++c) if (s.itemOf[c] >= 0) mix((uint64_t)c * 31 + (uint64_t)s.itemOf[c]);
    return h;
}
} // namespace

// Scene-derived fingerprint is taken at save time from the live arrays; items
// and barriers never change identity during a run, only their state.
bool Solver::saveState(const std::string& path, std::string& err) const {
    Out o;
    o.f.open(path, std::ios::binary);
    if (!o.f) { err = "cannot write " + path; return false; }
    o.f.write(MAGIC, 8);
    o.pod(STATE_VERSION);
    o.str(profile.name);
    o.pod(nx); o.pod(ny); o.pod(nz); o.pod(dx); o.pod(ox); o.pod(oy); o.pod(oz);
    o.pod(fingerprint(*this));
    o.pod(time);
    o.pod(wind);
    o.vec(u); o.vec(v); o.vec(w);
    o.vec(fx); o.vec(fy); o.vec(fz);
    o.vec(ax_); o.vec(ay_); o.vec(az_); o.vec(lx_); o.vec(ly_); o.vec(lz_);
    o.vec(p); o.vec(S);
    o.vec(T); o.vec(Yo); o.vec(Yf); o.vec(Ys); o.vec(Ya); o.vec(Yp); o.vec(Yc); o.vec(Yw); o.vec(Yco);
    o.vec(nu); o.vec(Pb); o.vec(zone); o.vec(zoneP); o.vec(zoneOpen); o.vec(zoneVol_);
    o.vec(hrr); o.vec(lit);
    o.vec(blocked); o.vec(porous); o.vec(fuelMat); o.vec(itemOf);
    o.vec(fuelMass); o.vec(fuelMass0); o.vec(charMass); o.vec(fuelT); o.vec(fuelArea);
    o.vec(solidCells); o.vec(burnFrac); o.vec(ignE);
    o.vec(itemMass0); o.vec(itemPyro); o.vec(itemFlags);
    o.pod(maxVel); o.pod(maxNu);
    uint64_t nb = barriers.size();
    o.pod(nb);
    for (const Barrier& b : barriers) {
        o.pod(b.front); o.pod(b.back); o.pod(b.T); o.pod(b.mass); o.pod(b.damage);
        o.pod(b.leak); o.pod(b.failed); o.pod(b.userOpen);
    }
    uint64_t ni = ignitions.size();
    o.pod(ni);
    for (const Ignition& g : ignitions) { o.pod(g.cell); o.pod(g.power); o.pod(g.start); o.pod(g.duration); o.str(g.label); }
    uint64_t nbu = burners.size();
    o.pod(nbu);
    for (const Burner& b : burners) {
        o.pod(b.x0); o.pod(b.y0); o.pod(b.x1); o.pod(b.y1); o.pod(b.z); o.pod(b.peakW); o.pod(b.rampS);
        o.pod(b.start); o.pod(b.duration); o.str(b.label); o.vec(b.cells);
    }
    uint64_t ne = events.size();
    o.pod(ne);
    for (const SimEvent& e : events) { o.pod(e.time); o.str(e.text); }
    uint64_t nk = eventKeys_.size();
    o.pod(nk);
    for (const std::string& k : eventKeys_) o.str(k);
    o.pod(stats);
    o.pod(radClock_);
    o.vec(radElems_); o.vec(radAbs_); o.vec(radGas_);
    o.f.write(MAGIC, 8);   // trailer: detects truncated files
    if (!o.f) { err = "write failed: " + path; return false; }
    return true;
}

bool Solver::loadState(const std::string& path, std::string& err) {
    In in;
    in.f.open(path, std::ios::binary);
    if (!in.f) { err = "cannot open " + path; return false; }
    char m[8];
    if (!in.f.read(m, 8) || std::memcmp(m, MAGIC, 8) != 0) { err = path + " is not a Photon state file"; return false; }
    uint32_t ver = 0;
    in.pod(ver);
    if (ver != STATE_VERSION) {
        err = "state file format v" + std::to_string(ver) + " cannot be loaded by this build (needs v" + std::to_string(STATE_VERSION) + ")";
        return false;
    }
    std::string prof;
    in.str(prof);
    int fnx, fny, fnz;
    float fdx, fox, foy, foz;
    in.pod(fnx); in.pod(fny); in.pod(fnz); in.pod(fdx); in.pod(fox); in.pod(foy); in.pod(foz);
    uint64_t fp = 0;
    in.pod(fp);
    if (!in.ok) { err = "state file is truncated"; return false; }
    if (prof != profile.name)
        { err = "state was saved with the " + prof + " profile; this solver runs " + profile.name + " (grids differ)"; return false; }
    if (fnx != nx || fny != ny || fnz != nz || fdx != dx || fox != ox || foy != oy || foz != oz)
        { err = "state grid does not match this scene's grid"; return false; }
    // The fingerprint must be compared against the state as built from the scene,
    // before this run changed anything: itemOf and barriers do not change.
    if (fp != fingerprint(*this)) { err = "state was saved from a different scene"; return false; }
    const size_t NX = (size_t)(nx + 1) * ny * nz, NY = (size_t)nx * (ny + 1) * nz, NZ = (size_t)nx * ny * (nz + 1);
    in.pod(time);
    in.pod(wind);
    in.vec(u, NX); in.vec(v, NY); in.vec(w, NZ);
    in.vec(fx, NX); in.vec(fy, NY); in.vec(fz, NZ);
    in.vec(ax_, NX); in.vec(ay_, NY); in.vec(az_, NZ); in.vec(lx_, NX); in.vec(ly_, NY); in.vec(lz_, NZ);
    in.vec(p, N); in.vec(S, N);
    in.vec(T, N); in.vec(Yo, N); in.vec(Yf, N); in.vec(Ys, N); in.vec(Ya, N); in.vec(Yp, N);
    in.vec(Yc, N); in.vec(Yw, N); in.vec(Yco, N);
    in.vec(nu, N); in.vec(Pb, N); in.vec(zone, N); in.vec(zoneP); in.vec(zoneOpen); in.vec(zoneVol_);
    in.vec(hrr, N); in.vec(lit, N);
    in.vec(blocked, N); in.vec(porous, N); in.vec(fuelMat, N); in.vec(itemOf, N);
    in.vec(fuelMass, N); in.vec(fuelMass0, N); in.vec(charMass, N); in.vec(fuelT, N); in.vec(fuelArea, N);
    in.vec(solidCells); in.vec(burnFrac, N); in.vec(ignE, N);
    in.vec(itemMass0); in.vec(itemPyro); in.vec(itemFlags);
    in.pod(maxVel); in.pod(maxNu);
    uint64_t nb = 0;
    in.pod(nb);
    if (!in.ok || nb != barriers.size()) { err = "state barrier count does not match"; return false; }
    for (Barrier& b : barriers) {
        in.pod(b.front); in.pod(b.back); in.pod(b.T); in.pod(b.mass); in.pod(b.damage);
        in.pod(b.leak); in.pod(b.failed); in.pod(b.userOpen);
    }
    uint64_t ni = 0;
    in.pod(ni);
    if (!in.ok || ni > 100000) { err = "state file is damaged"; return false; }
    ignitions.assign(ni, Ignition{});
    for (Ignition& g : ignitions) { in.pod(g.cell); in.pod(g.power); in.pod(g.start); in.pod(g.duration); in.str(g.label); }
    uint64_t nbu = 0;
    in.pod(nbu);
    if (!in.ok || nbu > 100000) { err = "state file is damaged"; return false; }
    burners.assign(nbu, Burner{});
    for (Burner& b : burners) {
        in.pod(b.x0); in.pod(b.y0); in.pod(b.x1); in.pod(b.y1); in.pod(b.z); in.pod(b.peakW); in.pod(b.rampS);
        in.pod(b.start); in.pod(b.duration); in.str(b.label); in.vec(b.cells);
    }
    uint64_t ne = 0;
    in.pod(ne);
    if (!in.ok || ne > 10000000) { err = "state file is damaged"; return false; }
    events.assign(ne, SimEvent{});
    for (SimEvent& e : events) { in.pod(e.time); in.str(e.text); }
    uint64_t nk = 0;
    in.pod(nk);
    if (!in.ok || nk > 10000000) { err = "state file is damaged"; return false; }
    eventKeys_.assign(nk, std::string());
    for (std::string& k : eventKeys_) in.str(k);
    in.pod(stats);
    in.pod(radClock_);
    in.vec(radElems_); in.vec(radAbs_); in.vec(radGas_);
    char tr[8];
    if (!in.ok || !in.f.read(tr, 8) || std::memcmp(tr, MAGIC, 8) != 0) { err = "state file is truncated or damaged"; return false; }
    zonesDirty_ = false;
    zoneDP_.assign(zoneP.size(), 0.0f);
    return true;
}

} // namespace photon

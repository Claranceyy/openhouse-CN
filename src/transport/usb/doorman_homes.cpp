#include "doorman_homes.hpp"

#include <algorithm>
#include <cstdio>

namespace lh::usb {

std::vector<uint16_t> DoormanHomes(int count) {
  static constexpr uint16_t kTwo[] = {0x00FF, 0xFF00}, kThree[] = {0x001F, 0x03E0, 0xFC00};
  std::vector<uint16_t> homes;
  for (int i = 0; i < count; ++i) homes.push_back(count == 1 ? 0xFFFF : count == 2 ? kTwo[i] : kThree[i % 3]);
  return homes;
}

std::vector<uint16_t> DoormanHomePlanner::Homes(const std::vector<std::string>& dongles, double now) {
  change_.clear();
  if (dongles != order_) {
    order_ = dongles;
    dongles_.clear();
    const auto homes = DoormanHomes(int(dongles.size()));
    for (size_t i = 0; i < dongles.size(); ++i) dongles_[dongles[i]].home = homes[i];
    changedAt_ = now;
  } else if (dongles.size() >= 2 && dongles.size() <= size_t(kDoormanHomesMax) && now - changedAt_ >= kSwapEvery) {
    Rebalance(now);
  }
  std::vector<uint16_t> out;
  for (const auto& s : order_) out.push_back(dongles_[s].home);
  return out;
}

void DoormanHomePlanner::Observe(const std::string& dongle, uint16_t mask, const std::array<uint8_t, kDoormanChannels>& loss,
                                 double now) {
  const auto it = dongles_.find(dongle);
  if (it == dongles_.end() || now - changedAt_ < kSettle) return;
  Dongle& d = it->second;
  if (!mask || (mask & ~d.home)) return;   // from before its home changed
  for (int k = 0; k < kDoormanChannels; ++k) {
    if (!((d.home >> k) & 1)) continue;
    Channel& c = d.ch[size_t(k)];
    c.off = !((mask >> k) & 1);
    if (c.off) continue;
    c.loss = c.measured ? 0.7 * c.loss + 0.3 * loss[size_t(k)] : loss[size_t(k)];
    c.measured = true;
  }
  ++d.reports;
}

// The lower median of the channels it hops on, as the firmware judges inside a home.
double DoormanHomePlanner::Typical(const Dongle& d) const {
  std::vector<double> v;
  for (int k = 0; k < kDoormanChannels; ++k) {
    const Channel& c = d.ch[size_t(k)];
    if ((d.home >> k) & 1 && c.measured && !c.off) v.push_back(c.loss);
  }
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[(v.size() - 1) / 2];
}

bool DoormanHomePlanner::Good(const Dongle& d, int k, double typical) const {
  const Channel& c = d.ch[size_t(k)];
  return (d.home >> k) & 1 && c.measured && !c.off && c.loss <= typical + kBadPoints;
}

void DoormanHomePlanner::Rebalance(double now) {
  struct View { Dongle* d; const std::string* serial; double typical; std::vector<int> good; };
  std::vector<View> views;
  for (const auto& s : order_) {
    Dongle& d = dongles_[s];
    if (d.reports < kReportsNeeded) return;   // not everyone has measured its home yet
    View v{&d, &s, Typical(d), {}};
    if (v.typical >= kEverywherePercent) continue;   // loss everywhere: the link is full, more channels won't help
    for (int k = 0; k < kDoormanChannels; ++k)
      if (Good(d, k, v.typical)) v.good.push_back(k);
    // Best first: what it keeps hopping on are its first kKeep.
    std::sort(v.good.begin(), v.good.end(), [&](int a, int b) { return d.ch[size_t(a)].loss < d.ch[size_t(b)].loss; });
    views.push_back(std::move(v));
  }
  View* needy = nullptr;
  View* donor = nullptr;
  for (auto& v : views) {
    if (int(v.good.size()) < kKeep && (!needy || v.good.size() < needy->good.size())) needy = &v;
    if (int(v.good.size()) > kKeep && (!donor || v.good.size() > donor->good.size())) donor = &v;
  }
  if (!needy || !donor || needy == donor) return;
  // The donor's spare: its best good channel past the kKeep it hops on (another dongle's
  // measurements aren't comparable, so its own ranking decides).
  const int gift = donor->good[size_t(kKeep)];
  // The needy dongle's worst: one it switched off, else the most lossy, else one it never measured.
  int back = -1;
  double worst = -1;
  for (int k = 0; k < kDoormanChannels; ++k) {
    if (!((needy->d->home >> k) & 1) || std::find(needy->good.begin(), needy->good.end(), k) != needy->good.end()) continue;
    const Channel& c = needy->d->ch[size_t(k)];
    const double score = c.off ? 1000 + c.loss : c.measured ? c.loss : 500;
    if (score > worst) { worst = score; back = k; }
  }
  if (back < 0) return;
  needy->d->home = uint16_t((needy->d->home & ~(1u << back)) | (1u << gift));
  donor->d->home = uint16_t((donor->d->home & ~(1u << gift)) | (1u << back));
  // What each knew about the two channels stays with the channel's new owner unmeasured: a fresh look.
  needy->d->ch[size_t(gift)] = Channel{};
  donor->d->ch[size_t(back)] = Channel{};
  for (auto& [serial, d] : dongles_) d.reports = 0;
  changedAt_ = now;
  char b[200];
  std::snprintf(b, sizeof b, "%s takes %d MHz from %s and gives back %d MHz (%zu good channels of its own)", needy->serial->c_str(),
                2404 + 5 * gift, donor->serial->c_str(), 2404 + 5 * back, needy->good.size());
  change_ = b;
}

}  // namespace lh::usb

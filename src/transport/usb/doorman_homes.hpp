// Which radio channels each Doorman Dongle hops on, when several run side by side.
//
// Two dongles a meter apart drown each other out on a shared channel, so each gets channels of
// its own (a "home", dongle cmd 0x36): fixed blocks to start (DoormanHomes), then adjusted to
// what the dongles measure. Each dongle reports its loss per channel (cmd 0x27) and switches bad
// channels off itself; a dongle left with fewer good channels than it hops on (4) takes a good
// spare from the dongle with the most and gives back its worst. Homes keep their size (the
// firmware's minimum: 8 before 0x0030, 5 from it).
//
// A channel is judged against its dongle's own typical channel, as the firmware does: a dongle
// whose devices are far away loses more everywhere, which isn't the channels' fault.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace lh::usb {

constexpr int kDoormanChannels = 16;

// The starting split for `count` dongles, in serial order. One: all 16. Two: the lower and upper
// 8. Three: 5 + 5 + 6 in blocks. More: the three-way split again, shared.
std::vector<uint16_t> DoormanHomes(int count);
constexpr int kDoormanHomesMax = 3;   // more dongles than this share channels: the driver warns

class DoormanHomePlanner {
 public:
  // Tuning, as the dongle firmware's (link.c): a channel more than kBadPoints over the dongle's
  // typical one is bad; at kEverywherePercent typical loss the link is full, not the channels.
  static constexpr int kKeep = 4;
  static constexpr double kBadPoints = 8, kEverywherePercent = 20;
  static constexpr double kSwapEvery = 30;     // s between changes
  static constexpr double kSettle = 6;         // s after a change before reports count (a new round)
  static constexpr int kReportsNeeded = 3;     // per dongle since the last change

  // Each dongle's home now, for `dongles` (serials, in the order homes are handed out). A new set
  // of dongles starts again from DoormanHomes.
  std::vector<uint16_t> Homes(const std::vector<std::string>& dongles, double now);
  // A channel report: the mask the dongle hops on and its loss % per channel over its last 2 s
  // round (channels it didn't poll read 0: only those in `mask` count).
  void Observe(const std::string& dongle, uint16_t mask, const std::array<uint8_t, kDoormanChannels>& loss, double now);
  // What the last Homes() call changed ("" nothing), for the log.
  const std::string& Change() const { return change_; }

 private:
  struct Channel {
    double loss = 0;
    bool measured = false;
    bool off = false;   // in its home, switched off by the dongle: it found it bad
  };
  struct Dongle {
    uint16_t home = 0;
    std::array<Channel, kDoormanChannels> ch{};
    int reports = 0;    // since the last change
  };
  double Typical(const Dongle& d) const;
  bool Good(const Dongle& d, int k, double typical) const;
  void Rebalance(double now);

  std::vector<std::string> order_;
  std::map<std::string, Dongle> dongles_;
  double changedAt_ = -1e300;
  std::string change_;
};

}  // namespace lh::usb

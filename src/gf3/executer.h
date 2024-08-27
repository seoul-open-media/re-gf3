#pragma once

#include "frame_makers/frame_makers.h"
#include "oneshots/oneshots.h"
#include "servo_units/gf3.h"

namespace gf3 {

class Executer {
 public:
  Executer(GF3& gf3) : gf3_{gf3} {}

  void Run() {
    // First process GF3-level Oneshots.
    {
      std::lock_guard lock{gf3_.cmd_.mtx};
      GF3Oneshots::Shoot(&gf3_);
    }

    // Query and distribute Replies.
    std::vector<CanFdFrame> query_frames;
    std::vector<CanFdFrame> reply_frames;
    static uint8_t query_group_label = 0;
    if (++query_group_label == 4) query_group_label = 0;
    std::set<int> query_group;
    switch (query_group_label) {
      case 0:
        query_group = {1, 2, 3, 13};
        break;
      case 1:
        query_group = {7, 8, 9, 14};
        break;
      case 2:
        query_group = {4, 5, 6, 13};
        break;
      case 3:
        query_group = {10, 11, 12, 14};
        break;
      default:
        break;
    }
    for (const auto id : query_group) {
      const auto maybe_servo = utils::SafeAt(gf3_.servo_map_, id);
      if (maybe_servo) {
        query_frames.push_back(maybe_servo.value()->MakeQuery());
      }
    }
    globals::transport->BlockingCycle(&query_frames[0], query_frames.size(),
                                      &reply_frames);
    for (const auto& frame : reply_frames) {
      const auto& id = static_cast<int>(frame.source);
      const auto maybe_servo = utils::SafeAt(gf3_.servo_map_, id);
      if (!maybe_servo) continue;
      maybe_servo.value()->SetReply(Query::Parse(frame.data, frame.size));
    }

    // Execute ServoUnit Mode-based Commands.
    std::vector<CanFdFrame> command_frames;
    for (const auto& j : gf3_.saj_set_) {
      std::lock_guard lock{j->cmd_.mtx};
      const auto maybe_fm =
          utils::SafeAt(SingleAxisJointFrameMakers::frame_makers, j->cmd_.mode);
      if (maybe_fm) {
        utils::Merge(command_frames, maybe_fm.value()(j));
      } else {
        std::cout
            << "Mode " << ((uint8_t)(j->cmd_.mode))
            << " NOT registered to SingleAxisJointFrameMakers::frame_makers."
            << std::endl;
      }
    }
    for (const auto& j : gf3_.dj_set_) {
      std::lock_guard lock{j->cmd_.mtx};
      const auto maybe_fm = utils::SafeAt(
          DifferentialJointFrameMakers::frame_makers, j->cmd_.mode);
      if (maybe_fm) {
        utils::Merge(command_frames, maybe_fm.value()(j));
      } else {
        std::cout
            << "Mode " << ((uint8_t)(j->cmd_.mode))
            << " NOT registered to DifferentialJointFrameMakers::frame_makers."
            << std::endl;
      }
    }

    globals::transport->BlockingCycle(&command_frames[0], command_frames.size(),
                                      nullptr);
  }

 private:
  GF3& gf3_;
};

}  // namespace gf3

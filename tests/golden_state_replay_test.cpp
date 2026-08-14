#include <cstring>
#include <memory>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "gsr/demo_domain.h"
#include "gsr/demo_exporter.h"
#include "gsr/replay_loader.h"

namespace {

class GoldenStateReplayTest : public ::testing::Test {
 protected:
  void TearDown() override { call_demo::ResetGlobals(); }
};

TEST_F(GoldenStateReplayTest, RestoresGraphAndReplaysRemoteHold) {
  auto replay = gsr::ReplayContext::LoadFile(
      std::string(GSR_SOURCE_DIR) + "/examples/connected_call.gsr");

  ASSERT_NE(call_demo::g_call, nullptr);
  ASSERT_NE(call_demo::g_active_leg, nullptr);
  ASSERT_NE(call_demo::g_call->peer, nullptr);
  EXPECT_TRUE(call_demo::g_service_ready);
  EXPECT_EQ(call_demo::g_call->active_leg,
            call_demo::g_active_leg);  // alias
  EXPECT_EQ(call_demo::g_call->peer->owner, call_demo::g_call);  // cycle
  ASSERT_NE(call_demo::g_active_leg->sibling, nullptr);
  EXPECT_EQ(call_demo::g_active_leg->sibling->sibling,
            call_demo::g_active_leg);  // cycle
  EXPECT_EQ(call_demo::g_active_leg->id, 20);  // offset 1

  ASSERT_EQ(replay->expected_calls().size(), 1U);
  call_demo::RecordingPlatformApi platform;
  EXPECT_EQ(call_demo::HandleRemoteHold(platform),
            call_demo::HandlerResult::kOk);
  EXPECT_EQ(platform.stopped_timer_tokens,
            std::vector<std::int32_t>({
                replay->expected_calls().front().argument}));

  std::string mismatch;
  EXPECT_TRUE(replay->OracleMatches(&mismatch)) << mismatch;
}

TEST_F(GoldenStateReplayTest, CapturedTextCanBeLoadedAgain) {
  auto call = std::make_unique<call_demo::CallSession>();
  auto peer = std::make_unique<call_demo::Peer>();
  auto legs = std::make_unique<call_demo::MediaLeg[]>(2);
  auto timer = std::make_unique<call_demo::TimerHandle>();
  call->call_id = 42;
  call->state = call_demo::CallState::kConnected;
  call->peer = peer.get();
  call->active_leg = &legs[1];
  call->refresh_timer = timer.get();
  std::strcpy(peer->uri, "sip:alice@example.test");
  peer->owner = call.get();
  legs[0] = {10, false, call.get(), &legs[1]};
  legs[1] = {20, true, call.get(), &legs[0]};
  timer->token = 9001;
  call_demo::g_service_ready = true;
  call_demo::g_call = call.get();
  call_demo::g_active_leg = &legs[1];

  std::ostringstream snapshot;
  gsr::ExportDemoState(snapshot, {call.get(), peer.get(), legs.get(), 2});
  call_demo::ResetGlobals();

  std::istringstream input(snapshot.str());
  auto replay = gsr::ReplayContext::Load(input);
  EXPECT_EQ(call_demo::g_call->active_leg, call_demo::g_active_leg);
  EXPECT_EQ(call_demo::g_active_leg->sibling->sibling,
            call_demo::g_active_leg);
  EXPECT_EQ(replay->metadata().at("capture_point"),
            "CALL_CONNECTED_QUIESCENT");
}

TEST_F(GoldenStateReplayTest, RejectsUnknownPointerTargets) {
  std::istringstream bad_snapshot(
      "GSR/1\n"
      "object call CallSession 1\n"
      "edge root 0 g_call missing 0\n");
  EXPECT_THROW(gsr::ReplayContext::Load(bad_snapshot), gsr::ProtocolError);
}

}  // namespace

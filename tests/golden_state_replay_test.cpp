#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gsr/demo_domain.h"
#include "gsr/replay_loader.h"

namespace {

class GoldenStateReplayTest : public ::testing::Test {
 protected:
  void TearDown() override { call_demo::ResetGlobals(); }
};

TEST_F(GoldenStateReplayTest, ReplaysFunctionEntryWithTypedCodec) {
  auto replay = gsr::ReplayContext::LoadBundle(
      std::string(GSR_SOURCE_DIR) + "/examples/invite_200_ok");
  const auto& args = replay->arguments();

  ASSERT_NE(args.sip_ptr, nullptr);
  EXPECT_EQ(args.service_ptr, &args.sip_ptr->service);
  EXPECT_EQ(call_demo::g_runtime, args.sip_ptr);
  EXPECT_TRUE(call_demo::g_feature_enabled);
  EXPECT_EQ(args.event, 200);
  EXPECT_EQ(args.ua_event_ptr->header.type,
            call_demo::UaEventType::kResponse);

  auto* ua = static_cast<call_demo::UaContext*>(args.h_ua);
  auto* dialog = static_cast<call_demo::Dialog*>(args.h_dialog);
  ASSERT_NE(ua, nullptr);
  ASSERT_NE(dialog, nullptr);
  EXPECT_EQ(args.sip_ptr->primary_ua, ua);
  EXPECT_EQ(args.ua_event_ptr->header.ua, args.h_ua);
  EXPECT_EQ(args.ua_event_ptr->header.owner, args.h_dialog);
  EXPECT_EQ(ua->dialogs.head, &dialog->link);
  EXPECT_EQ(ua->dialogs.tail, &dialog->link);

  EXPECT_EQ(args.event_ptr->correlation_id, 73);
  EXPECT_STREQ(args.event_ptr->call.to, "");

  call_demo::RecordingExternalApi external;
  call_demo::SetExternalApi(&external);
  EXPECT_EQ(call_demo::ProcessSipCallEvent(
                args.sip_ptr, args.service_ptr, args.h_ua, args.h_dialog,
                args.event, args.ua_event_ptr, args.event_ptr),
            call_demo::HandlerResult::kOk);

  ASSERT_EQ(replay->expected_calls().size(), 1U);
  EXPECT_EQ(external.confirmed_dialog_ids,
            std::vector<std::int32_t>({
                replay->expected_calls().front().argument}));
  std::string mismatch;
  EXPECT_TRUE(replay->OracleMatches(&mismatch)) << mismatch;
}

TEST_F(GoldenStateReplayTest, RejectsABIMismatchBeforeAllocatingState) {
  const std::string bad_bundle =
      std::string(GSR_BINARY_DIR) + "/bad-fingerprint-bundle";
  std::filesystem::create_directories(bad_bundle);
  {
    std::ofstream manifest(bad_bundle + "/manifest.json");
    manifest << "{\"format\":\"golden-state-replay\","
                "\"version\":2,"
                "\"target\":\"ProcessSipCallEvent\","
                "\"capture_point\":\"function_entry\","
                "\"type_fingerprint\":\"wrong\","
                "\"pointer_size\":"
             << sizeof(void*) << "}";
  }
  EXPECT_THROW(gsr::ReplayContext::LoadBundle(bad_bundle),
               gsr::ProtocolError);
}

}  // namespace

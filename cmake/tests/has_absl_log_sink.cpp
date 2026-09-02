// Purely a compile-time check: decltype only needs the declarations to
// determine types, never an actual call/link against a not-yet-built
// Abseil (on the FetchContent path, Abseil is built as part of gRPC's
// own source tree, alongside Protobuf - it doesn't exist on disk yet at
// this configure-time check either).
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"

class DrEvtAbslLogSinkCheck : public absl::LogSink {
public:
  void Send(const absl::LogEntry&) override {}
};

using AddSinkType = decltype(&absl::AddLogSink);
using RemoveSinkType = decltype(&absl::RemoveLogSink);

int main()
{
  return 0;
}

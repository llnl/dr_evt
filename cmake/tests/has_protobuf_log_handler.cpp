// Purely a compile-time check: decltype only needs SetLogHandler's
// declaration to determine its type, never its actual address in a way
// that would need linking against libprotobuf - unlike calling it (as an
// earlier version of this file did), which requires an already-built
// library. That's a real requirement here, not a theoretical one: on
// the FetchContent path, this test runs during the parent project's own
// configure step, before gRPC's bundled Protobuf has actually been
// compiled - the library file genuinely doesn't exist on disk yet at
// this point, regardless of how it's referenced.
#include <google/protobuf/stubs/common.h>

using LogHandlerType = decltype(&google::protobuf::SetLogHandler);

int main()
{
  return 0;
}

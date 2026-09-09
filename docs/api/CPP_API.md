# C++ API Reference

This is the entry point for the generated C++ reference. Start with a
responsibility when you know the subsystem you are changing, or with a source
directory when you are navigating the implementation. Each category has its
own page, including the generated declarations and a page-local filter where
applicable.

<div class="api-search" role="search">
  <label for="cpp-api-map-search">Search C++ API categories</label>
  <input id="cpp-api-map-search" data-api-search="cpp-api-map-content" type="search" placeholder="e.g., Trace, backfill, gRPC" autocomplete="off">
  <span class="api-search-status" aria-live="polite"></span>
</div>

<div id="cpp-api-map-content" class="api-search-content api-search-map">

## Browse by responsibility

```{toctree}
:maxdepth: 1

cpp/topics/job-store
cpp/topics/trace-replay
cpp/topics/resource-trace
cpp/topics/scheduling-backfill
cpp/topics/simulator
cpp/topics/client-server
cpp/topics/bindings
cpp/topics/configuration
cpp/topics/utilities
```

## Browse by source directory

```{toctree}
:maxdepth: 1

cpp/source/src
cpp/source/sim
cpp/source/trace
cpp/source/params
cpp/source/proto
cpp/source/mpi
cpp/source/utils
```

</div>

The generated reference is available only in builds where Doxygen XML is
present. The topic pages remain useful as an API map even when that optional
reference is unavailable.

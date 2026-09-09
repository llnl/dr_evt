# C++ API Reference

This is the entry point for the generated C++ reference. Start with a
responsibility when you know the subsystem you are changing, or with a source
directory or file when you are navigating the implementation. Each category has its
own page, including the generated declarations and a page-local filter where
applicable.

<form class="api-search api-symbol-search" action="../search.html" method="get" role="search">
  <label for="cpp-api-symbol-search">Search C++ symbols</label>
  <input id="cpp-api-symbol-search" name="q" type="search" placeholder="e.g., append_job, insert_job, Sim_Params" autocomplete="off">
  <button type="submit">Search</button>
  <span class="api-search-help">Searches generated C++ declarations by symbol name.</span>
</form>

Search by the API's exact C++ name (for example, `append_job`, `insert_job`,
or `submit_job`); `submit_to` and `insert_to` are not DR_EVT symbols. The
symbol search uses the documentation-wide index, so results may also include
guides that mention the symbol. Generated C++ declaration results are
available when the build includes Doxygen XML.

<div class="api-search" role="search">
  <label for="cpp-api-map-search">Search C++ API categories</label>
  <input id="cpp-api-map-search" data-api-search="cpp-api-map-content" type="search" placeholder="e.g., Trace, backfill, gRPC" autocomplete="off">
  <span class="api-search-status" aria-live="polite"></span>
</div>

<div id="cpp-api-map-content" class="api-search-content api-search-map">

## Browse by responsibility

```{toctree}
:maxdepth: 1

cpp/topics/job-store.rst
cpp/topics/trace-replay.rst
cpp/topics/resource-trace.rst
cpp/topics/scheduling-backfill.rst
cpp/topics/simulator.rst
cpp/topics/client-server.rst
cpp/topics/bindings
cpp/topics/configuration.rst
cpp/topics/utilities.rst
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

## Browse by code file

```{toctree}
:maxdepth: 1

cpp/files
```

</div>

The generated reference is available only in builds where Doxygen XML is
present. The topic pages remain useful as an API map even when that optional
reference is unavailable.

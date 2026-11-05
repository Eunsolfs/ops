#pragma once

// Single source of truth for the OP version.
// Keep this file ASCII-only: it is included by the resource compiler (.rc),
// whose preprocessor runs with the ANSI code page and rejects UTF-8 comments.
// Update these four macros; vs/op.common.props keeps the matching OP_VERSION
// string and op_test's VersionMatchesHeader case guards the drift.
#define OP_VERSION_MAJOR 0
#define OP_VERSION_MINOR 4
#define OP_VERSION_PATCH 8
#define OP_VERSION_BUILD 3

#define OP_VERSION_STRING "0.4.8.3"
#define OP_VERSION_STRING_W L"0.4.8.3"

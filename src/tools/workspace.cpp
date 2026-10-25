// src/tools/workspace.cpp — Workspace::resolve (spec §4.4).
//
// A purely lexical check: it never touches the filesystem, so it works for
// files that do not exist yet. Symbolic links are handled afterwards by the
// provided check_symlinks().

#include "tools/workspace.h"

#include <exception>
#include <string>

namespace fs = std::filesystem;

bool Workspace::resolve(std::string_view user_path, fs::path* out,
                        std::string* reason) const {
    // Step 1: empty path.
    if (user_path.empty()) {
        *reason = "empty path";
        return false;
    }

    // Step 2: an embedded NUL would silently truncate the path in the OS.
    if (user_path.find('\0') != std::string_view::npos) {
        *reason = "path contains a NUL byte";
        return false;
    }

    try {
        // Step 3: only workspace-relative paths are allowed.
        const fs::path p{std::string(user_path)};
        if (p.is_absolute() || p.has_root_path()) {
            *reason = "absolute paths are not allowed: " + std::string(user_path);
            return false;
        }

        // Step 4: remove "." components, duplicate slashes, fold "x/..".
        const fs::path n = p.lexically_normal();

        // Step 5: a leading ".." after normalization means the path climbs
        // above the workspace root at some point.
        if (n.empty() || *n.begin() == "..") {
            *reason = "path escapes the workspace: " + std::string(user_path);
            return false;
        }

        // Step 6: map to an absolute path inside root().
        if (n == ".") {
            *out = root();
        } else {
            *out = root() / n;
        }
        return true;
    } catch (const std::exception& e) {
        // fs::path construction can allocate; never let anything escape.
        *reason = std::string("invalid path: ") + e.what();
        return false;
    }
}

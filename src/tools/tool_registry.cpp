// src/tools/tool_registry.cpp — hash table from tool name to Tool (spec §4.3).
//
// Design: separate chaining, FNV-1a (64-bit) hash, power-of-two bucket count,
// maximum load factor 1.0. The table doubles BEFORE an insert that would push
// the load factor above 1.0, so load_factor() <= 1.0 whenever add() returns.

#include "tools/tool_registry.h"

#include <cstdint>
#include <string>
#include <utility>

ToolRegistry::ToolRegistry() : buckets_(kInitialBuckets) {}

// Every Entry owns its Tool through a unique_ptr, so destroying buckets_
// destroys each tool exactly once.
ToolRegistry::~ToolRegistry() = default;

// FNV-1a, 64-bit: uses every byte, order-sensitive, returns the full-width
// value (reduced to an index separately by index_for).
std::size_t ToolRegistry::hash(std::string_view key) noexcept {
    std::uint64_t h = 1469598103934665603ULL;    // offset basis
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;                   // FNV prime
    }
    return static_cast<std::size_t>(h);
}

void ToolRegistry::rehash(std::size_t new_count) {
    std::vector<Bucket> fresh(new_count);
    for (Bucket& bucket : buckets_) {
        for (Entry& e : bucket) {
            // Moving the Entry moves the unique_ptr (just a pointer);
            // the Tool object stays at the same address.
            fresh[index_for(e.hash, new_count)].push_back(std::move(e));
        }
    }
    buckets_.swap(fresh);
}

bool ToolRegistry::add(std::unique_ptr<Tool> tool) {
    if (tool == nullptr) return false;

    std::string name = tool->name();
    const std::size_t h = hash(name);

    // Duplicate check first: a rejected add must not resize the table.
    for (const Entry& e : buckets_[index_for(h, buckets_.size())]) {
        if (e.hash == h && e.name == name) {
            return false;   // `tool` goes out of scope here: the NEW tool is destroyed
        }
    }

    // Grow before inserting so the load factor never exceeds the threshold.
    if (static_cast<double>(size_ + 1) >
        kMaxLoadFactor * static_cast<double>(buckets_.size())) {
        rehash(buckets_.size() * 2);
    }

    buckets_[index_for(h, buckets_.size())].push_back(
        Entry{std::move(name), h, std::move(tool)});
    ++size_;
    return true;
}

Tool* ToolRegistry::find(std::string_view name) const {
    const std::size_t h = hash(name);
    for (const Entry& e : buckets_[index_for(h, buckets_.size())]) {
        if (e.hash == h && e.name == name) return e.tool.get();
    }
    return nullptr;
}

std::size_t ToolRegistry::size() const noexcept {
    return size_;
}

void ToolRegistry::for_each(const std::function<void(const Tool&)>& fn) const {
    for (const Bucket& bucket : buckets_) {
        for (const Entry& e : bucket) fn(*e.tool);
    }
}

std::size_t ToolRegistry::bucket_count() const noexcept {
    return buckets_.size();
}

double ToolRegistry::load_factor() const noexcept {
    return static_cast<double>(size_) / static_cast<double>(buckets_.size());
}

std::size_t ToolRegistry::max_probe_length() const {
    std::size_t longest = 0;
    for (const Bucket& bucket : buckets_) {
        if (bucket.size() > longest) longest = bucket.size();
    }
    return longest;
}

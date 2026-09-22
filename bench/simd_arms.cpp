#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <experimental/simd>
#include <string>
#include <string_view>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

// Two ways to find the five HTML marks in a value and write it escaped:
// the intrinsics as mruby-mustache carries them today, and
// std::experimental::simd, which is std::simd under its C++26 name.
// Same inputs, same binary, alternating; the question is whether the
// standard type costs anything against the hand written form.

namespace
{

namespace stdx = std::experimental;
using bytes = stdx::native_simd<unsigned char>;

constexpr std::string_view kMarks = "&<>\"'";

constexpr std::array<std::string_view, 256> kEscaped = [] {
    std::array<std::string_view, 256> t{};
    t['&'] = "&amp;";
    t['<'] = "&lt;";
    t['>'] = "&gt;";
    t['"'] = "&quot;";
    t['\''] = "&#39;";
    return t;
}();

// --- the scan, three ways ------------------------------------------------

size_t mark_plain(const std::string_view value, size_t from)
{
    for (; from < value.size(); from++) {
        const char c = value[from];
        if (c == '&' || c == '<' || c == '>' || c == '"' || c == '\'') return from;
    }
    return value.size();
}

#if defined(__AVX2__)
size_t mark_intrinsics(const std::string_view value, size_t from)
{
    const unsigned char *src = reinterpret_cast<const unsigned char *>(value.data());
    const __m256i amp = _mm256_set1_epi8('&');
    const __m256i lt = _mm256_set1_epi8('<');
    const __m256i gt = _mm256_set1_epi8('>');
    const __m256i quo = _mm256_set1_epi8('"');
    const __m256i apo = _mm256_set1_epi8('\'');
    for (; from + 32 <= value.size(); from += 32) {
        const __m256i block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + from));
        const __m256i hit = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(block, amp), _mm256_cmpeq_epi8(block, lt)),
            _mm256_or_si256(_mm256_cmpeq_epi8(block, gt),
                            _mm256_or_si256(_mm256_cmpeq_epi8(block, quo),
                                            _mm256_cmpeq_epi8(block, apo))));
        const uint32_t found = static_cast<uint32_t>(_mm256_movemask_epi8(hit));
        if (found != 0) return from + static_cast<size_t>(__builtin_ctz(found));
    }
    return mark_plain(value, from);
}
#endif

size_t mark_simd(const std::string_view value, size_t from)
{
    constexpr auto byte = [](char c) { return static_cast<unsigned char>(c); };
    const bytes amp(byte('&')), lt(byte('<')), gt(byte('>')), quo(byte('"')), apo(byte('\''));
    for (; from + bytes::size() <= value.size(); from += bytes::size()) {
        bytes block;
        block.copy_from(reinterpret_cast<const unsigned char *>(value.data()) + from,
                        stdx::element_aligned);
        const auto hit = (block == amp) | (block == lt) | (block == gt) | (block == quo) |
                         (block == apo);
        if (stdx::any_of(hit)) return from + stdx::find_first_set(hit);
    }
    const size_t tail = value.find_first_of(kMarks, from);
    return tail == std::string_view::npos ? value.size() : tail;
}

// --- the escape, on top of each scan ------------------------------------

template <size_t (*first_mark)(std::string_view, size_t)>
void escape_append(std::string &out, const std::string_view value)
{
    size_t from = 0;
    while (from < value.size()) {
        const size_t mark = first_mark(value, from);
        if (mark == value.size()) {
            out.append(value.substr(from));
            return;
        }
        out.append(value.substr(from, mark - from));
        out.append(kEscaped[static_cast<unsigned char>(value[mark])]);
        from = mark + 1;
    }
}

#if defined(__AVX2__)
// As the gem writes it today: room for the worst case once, then memcpy.
void escape_memcpy(std::string &out, const std::string_view value)
{
    const size_t was = out.size();
    out.resize_and_overwrite(was + value.size() * 6, [&](char *const bytes, size_t) -> size_t {
        char *w = bytes + was;
        size_t from = 0;
        while (from < value.size()) {
            const size_t mark = mark_intrinsics(value, from);
            std::memcpy(w, value.data() + from, mark - from);
            w += mark - from;
            if (mark == value.size()) break;
            const std::string_view name = kEscaped[static_cast<unsigned char>(value[mark])];
            std::memcpy(w, name.data(), name.size());
            w += name.size();
            from = mark + 1;
        }
        return static_cast<size_t>(w - bytes);
    });
}
#endif

// --- the escape that keeps the mask ---------------------------------------
//
// The forms above restart the scan after every mark, so at one mark in
// twenty a 32 byte block is loaded and compared again for each of its
// marks. This one loads a block once, takes its mask once, and walks the
// set bits; the block is looked at once whatever its density.

void escape_simd_mask(std::string &out, const std::string_view value)
{
    constexpr auto byte = [](char c) { return static_cast<unsigned char>(c); };
    const bytes amp(byte('&')), lt(byte('<')), gt(byte('>')), quo(byte('"')), apo(byte('\''));
    size_t from = 0;   // first byte not yet written
    size_t at = 0;     // start of the block being looked at
    for (; at + bytes::size() <= value.size(); at += bytes::size()) {
        bytes block;
        block.copy_from(reinterpret_cast<const unsigned char *>(value.data()) + at,
                        stdx::element_aligned);
        auto hit = (block == amp) | (block == lt) | (block == gt) | (block == quo) |
                   (block == apo);
        while (stdx::any_of(hit)) {
            const size_t i = at + static_cast<size_t>(stdx::find_first_set(hit));
            out.append(value.substr(from, i - from));
            out.append(kEscaped[static_cast<unsigned char>(value[i])]);
            from = i + 1;
            hit[i - at] = false;
        }
    }
    // What the blocks did not cover is looked at once more, from `at`:
    // every byte before it has been seen, and `from` only says what has
    // not been written yet.
    for (size_t mark = value.find_first_of(kMarks, at); mark != std::string_view::npos;
         mark = value.find_first_of(kMarks, mark + 1)) {
        out.append(value.substr(from, mark - from));
        out.append(kEscaped[static_cast<unsigned char>(value[mark])]);
        from = mark + 1;
    }
    out.append(value.substr(from));
}

// The same, but the mask is never written to: after a mark at lane i the
// mask is and-ed with "lane > i", a vector compare against the lane
// numbers. Every operation stays a whole-vector operation.
void escape_simd_lanes(std::string &out, const std::string_view value)
{
    constexpr auto byte = [](char c) { return static_cast<unsigned char>(c); };
    const bytes amp(byte('&')), lt(byte('<')), gt(byte('>')), quo(byte('"')), apo(byte('\''));
    const bytes lane([](auto i) { return static_cast<unsigned char>(i); });
    size_t from = 0, at = 0;
    for (; at + bytes::size() <= value.size(); at += bytes::size()) {
        bytes block;
        block.copy_from(reinterpret_cast<const unsigned char *>(value.data()) + at,
                        stdx::element_aligned);
        auto hit = (block == amp) | (block == lt) | (block == gt) | (block == quo) |
                   (block == apo);
        while (stdx::any_of(hit)) {
            const int lane_hit = stdx::find_first_set(hit);
            const size_t i = at + static_cast<size_t>(lane_hit);
            out.append(value.substr(from, i - from));
            out.append(kEscaped[static_cast<unsigned char>(value[i])]);
            from = i + 1;
            hit = hit & (lane > bytes(static_cast<unsigned char>(lane_hit)));
        }
    }
    // What the blocks did not cover is looked at once more, from `at`:
    // every byte before it has been seen, and `from` only says what has
    // not been written yet.
    for (size_t mark = value.find_first_of(kMarks, at); mark != std::string_view::npos;
         mark = value.find_first_of(kMarks, mark + 1)) {
        out.append(value.substr(from, mark - from));
        out.append(kEscaped[static_cast<unsigned char>(value[mark])]);
        from = mark + 1;
    }
    out.append(value.substr(from));
}

#if defined(__AVX2__)
void escape_intrinsics_mask(std::string &out, const std::string_view value)
{
    const unsigned char *src = reinterpret_cast<const unsigned char *>(value.data());
    const __m256i amp = _mm256_set1_epi8('&');
    const __m256i lt = _mm256_set1_epi8('<');
    const __m256i gt = _mm256_set1_epi8('>');
    const __m256i quo = _mm256_set1_epi8('"');
    const __m256i apo = _mm256_set1_epi8('\'');
    size_t from = 0, at = 0;
    for (; at + 32 <= value.size(); at += 32) {
        const __m256i block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + at));
        const __m256i hit = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(block, amp), _mm256_cmpeq_epi8(block, lt)),
            _mm256_or_si256(_mm256_cmpeq_epi8(block, gt),
                            _mm256_or_si256(_mm256_cmpeq_epi8(block, quo),
                                            _mm256_cmpeq_epi8(block, apo))));
        uint32_t found = static_cast<uint32_t>(_mm256_movemask_epi8(hit));
        while (found != 0) {
            const size_t i = at + static_cast<size_t>(__builtin_ctz(found));
            out.append(value.substr(from, i - from));
            out.append(kEscaped[static_cast<unsigned char>(value[i])]);
            from = i + 1;
            found &= found - 1;
        }
    }
    for (size_t mark = mark_plain(value, at); mark != value.size();
         mark = mark_plain(value, mark + 1)) {
        out.append(value.substr(from, mark - from));
        out.append(kEscaped[static_cast<unsigned char>(value[mark])]);
        from = mark + 1;
    }
    out.append(value.substr(from));
}
#endif

// --- inputs ---------------------------------------------------------------

std::string with_marks(size_t length, size_t every)
{
    std::string s(length, 'x');
    for (size_t i = every; i < length; i += every) s[i] = "&<>\"'"[i % 5];
    return s;
}

const std::string kPlain1K(1024, 'x');
const std::string kMarked1K = with_marks(1024, 20);  // 5 percent marks
const std::string kShort = "Sch\"on & <gut> mit 'Note' hier";  // a real value

template <size_t (*scan)(std::string_view, size_t)>
void scan_arm(benchmark::State &state, const std::string &input)
{
    for (auto _ : state) {
        size_t from = 0, hits = 0;
        while (from < input.size()) {
            from = scan(input, from) + 1;
            hits++;
        }
        benchmark::DoNotOptimize(hits);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * input.size()));
}

template <void (*escape)(std::string &, std::string_view)>
void escape_arm(benchmark::State &state, const std::string &input)
{
    std::string out;
    out.reserve(input.size() * 6);
    for (auto _ : state) {
        out.clear();
        escape(out, input);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * input.size()));
}

#if defined(__AVX2__)
void scan_plain1k_intrinsics(benchmark::State &s) { scan_arm<mark_intrinsics>(s, kPlain1K); }
void scan_marked1k_intrinsics(benchmark::State &s) { scan_arm<mark_intrinsics>(s, kMarked1K); }
void scan_short_intrinsics(benchmark::State &s) { scan_arm<mark_intrinsics>(s, kShort); }
void escape_plain1k_memcpy(benchmark::State &s) { escape_arm<escape_memcpy>(s, kPlain1K); }
void escape_marked1k_memcpy(benchmark::State &s) { escape_arm<escape_memcpy>(s, kMarked1K); }
void escape_short_memcpy(benchmark::State &s) { escape_arm<escape_memcpy>(s, kShort); }
void escape_marked1k_intrinsics_append(benchmark::State &s)
{
    escape_arm<escape_append<mark_intrinsics>>(s, kMarked1K);
}
void escape_short_intrinsics_append(benchmark::State &s)
{
    escape_arm<escape_append<mark_intrinsics>>(s, kShort);
}
#endif
void scan_plain1k_simd(benchmark::State &s) { scan_arm<mark_simd>(s, kPlain1K); }
void scan_marked1k_simd(benchmark::State &s) { scan_arm<mark_simd>(s, kMarked1K); }
void scan_short_simd(benchmark::State &s) { scan_arm<mark_simd>(s, kShort); }
void scan_marked1k_plain(benchmark::State &s) { scan_arm<mark_plain>(s, kMarked1K); }
void escape_plain1k_simd(benchmark::State &s) { escape_arm<escape_append<mark_simd>>(s, kPlain1K); }
void escape_marked1k_simd(benchmark::State &s) { escape_arm<escape_append<mark_simd>>(s, kMarked1K); }
void escape_short_simd(benchmark::State &s) { escape_arm<escape_append<mark_simd>>(s, kShort); }
void escape_marked1k_simd_mask(benchmark::State &s) { escape_arm<escape_simd_mask>(s, kMarked1K); }
void escape_plain1k_simd_mask(benchmark::State &s) { escape_arm<escape_simd_mask>(s, kPlain1K); }
void escape_short_simd_mask(benchmark::State &s) { escape_arm<escape_simd_mask>(s, kShort); }
void escape_marked1k_simd_lanes(benchmark::State &s) { escape_arm<escape_simd_lanes>(s, kMarked1K); }
void escape_plain1k_simd_lanes(benchmark::State &s) { escape_arm<escape_simd_lanes>(s, kPlain1K); }
void escape_short_simd_lanes(benchmark::State &s) { escape_arm<escape_simd_lanes>(s, kShort); }
#if defined(__AVX2__)
void escape_marked1k_intrinsics_mask(benchmark::State &s) { escape_arm<escape_intrinsics_mask>(s, kMarked1K); }
void escape_short_intrinsics_mask(benchmark::State &s) { escape_arm<escape_intrinsics_mask>(s, kShort); }
void escape_plain1k_intrinsics_mask(benchmark::State &s) { escape_arm<escape_intrinsics_mask>(s, kPlain1K); }
#endif

}  // namespace

#if defined(__AVX2__)
BENCHMARK(scan_plain1k_intrinsics);
BENCHMARK(scan_marked1k_intrinsics);
BENCHMARK(scan_short_intrinsics);
BENCHMARK(escape_plain1k_memcpy);
BENCHMARK(escape_marked1k_memcpy);
BENCHMARK(escape_short_memcpy);
BENCHMARK(escape_marked1k_intrinsics_append);
BENCHMARK(escape_short_intrinsics_append);
#endif
BENCHMARK(scan_plain1k_simd);
BENCHMARK(scan_marked1k_simd);
BENCHMARK(scan_short_simd);
BENCHMARK(scan_marked1k_plain);
BENCHMARK(escape_plain1k_simd);
BENCHMARK(escape_marked1k_simd);
BENCHMARK(escape_short_simd);
BENCHMARK(escape_marked1k_simd_mask);
BENCHMARK(escape_plain1k_simd_mask);
BENCHMARK(escape_short_simd_mask);
BENCHMARK(escape_marked1k_simd_lanes);
BENCHMARK(escape_plain1k_simd_lanes);
BENCHMARK(escape_short_simd_lanes);
#if defined(__AVX2__)
BENCHMARK(escape_marked1k_intrinsics_mask);
BENCHMARK(escape_short_intrinsics_mask);
BENCHMARK(escape_plain1k_intrinsics_mask);
#endif

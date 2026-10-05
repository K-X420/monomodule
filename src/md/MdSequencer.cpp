#include "MdSequencer.h"
#include <algorithm>
#include <cmath>

namespace mnm::md {

void PatternPlayer::set(const mddump::Pattern& p)
{
    m_p = p;
    m_len = std::clamp(int(p.length), 1, p.extended ? 64 : 32);
    m_step = stepClocks(p.doubleTempo);   // the multiplier byte (+0x92): 0 1x, 1 2x, 2 3/4x, 3 3/2x
    m_swing = double(m_step) * double(p.swingAmount) / 16384.0;
    m_swingWhole = int((int64_t(m_step) * int64_t(p.swingAmount)) >> 14);
    for (int t = 0; t < 16; ++t)
        for (int q = 0; q < 24; ++q) m_row[size_t(t)][size_t(q)] = int16_t(p.lockRow(t, q));
}

int PatternPlayer::lock(int track, int param, int step) const
{
    const int row = m_row[size_t(track)][size_t(param)];
    if (row < 0 || step < 0 || step >= 64) return -1;
    const int v = m_p.locks[row][step];
    return v <= 127 ? v : -1;
}

SeqTrig PatternPlayer::trigAt(int64_t stepIndex, int t) const
{
    SeqTrig e;
    e.stepIndex = stepIndex;
    e.step = int(stepIndex % m_len);
    e.track = t;
    e.clock = double(stepIndex) * m_step + (swung(t, e.step) ? m_swing : 0.0);
    e.accent = accented(t, e.step);
    e.locks.fill(-1);
    e.slideTo.fill(-1);
    for (int q = 0; q < 24; ++q) e.locks[size_t(q)] = int16_t(lock(t, q, e.step));
    if (slid(t, e.step)) {
        // the track's next trig: steps on (wrapping at the length; itself when it is the only one)
        int n = 1, next = (e.step + 1) % m_len;
        while (!hasTrig(t, next) && n < m_len) { ++n; next = (next + 1) % m_len; }
        int32_t clocks = n * m_step;
        const bool a = swung(t, e.step), b = swung(t, next);
        if (a != b) clocks += a ? -m_swingWhole : m_swingWhole;
        if (clocks == 0) clocks = 1;
        for (int q = 0; q < 24; ++q) {
            if (e.locks[size_t(q)] < 0) continue;
            e.slideMask |= 1u << q;
            e.slideTo[size_t(q)] = int16_t(lock(t, q, next));
            e.slideClocks[size_t(q)] = clocks;
        }
    }
    return e;
}

void PatternPlayer::trigs(double from, double to, std::vector<SeqTrig>& out) const
{
    if (to <= from) return;
    const size_t first = out.size();
    // a step's trigs are at its start, or a swing delay later
    int64_t k = std::max<int64_t>(0, int64_t(std::floor((from - m_swing) / m_step)));
    for (; double(k) * m_step < to; ++k) {
        const int step = int(k % m_len);
        for (int t = 0; t < 16; ++t) {
            if (!hasTrig(t, step)) continue;
            const double c = double(k) * m_step + (swung(t, step) ? m_swing : 0.0);
            if (c < from || c >= to) continue;
            out.push_back(trigAt(k, t));
        }
    }
    std::stable_sort(out.begin() + std::ptrdiff_t(first), out.end(), [](const SeqTrig& a, const SeqTrig& b) { return a.clock < b.clock; });
}

void Glide::start(int from, int to, int32_t clocks, double trigClock)
{
    value = from << 7;
    end = to << 7;
    rate = (end - value) / std::max<int32_t>(1, clocks);   // divs: toward zero
    active = rate != 0;
    nextClock = std::floor(trigClock) + 1.0;               // the first step at the next clock
}

bool Glide::advance(double now)
{
    bool changed = false;
    while (active && nextClock <= now) {
        const int32_t v = value + rate;
        if ((rate > 0 && v > end) || (rate < 0 && v < end)) { active = false; break; }   // stops short of the end
        if ((v >> 7) != (value >> 7)) changed = true;
        value = v;
        nextClock += 1.0;
    }
    return changed;
}

} // namespace mnm::md

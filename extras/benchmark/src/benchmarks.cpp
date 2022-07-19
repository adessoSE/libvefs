
#include <benchmark/benchmark.h>

#include <vefs/utils/bit.hpp>
#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/utils/random.hpp>

static void countnaive(benchmark::State &state)
{
    using namespace vefs::utils;

    xoroshiro128plus generator{0xDEADBEEF'C0DE4237ull};
    for (auto _ : state)
    {
        state.PauseTiming();
        std::array<std::byte, 64> mem;
        vefs::fill_blob(std::span(mem), std::byte{0b0101'0101});
        // generator.fill(mem);
        state.ResumeTiming();

        std::size_t r = 0;

        int start = -1;
        std::size_t num
                = mem.size() * std::numeric_limits<unsigned char>::digits;
        const_bitset_overlay data(mem);
        for (std::size_t i = 0; i < num; ++i)
        {
            if (data[i])
            {
                if (start >= 0)
                {
                    r += i - start;
                }
                start = -1;
            }
            else if (start < 0)
            {
                start = i;
            }
        }
        if (start >= 0)
        {
            r += num - start;
        }
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(countnaive);
static void countspecial(benchmark::State &state)
{
    using namespace vefs::utils;

    xoroshiro128plus generator{0xDEADBEEF'C0DE4237ull};
    for (auto _ : state)
    {
        state.PauseTiming();
        std::array<std::byte, 64> mem;
        // generator.fill(mem);
        vefs::fill_blob(std::span(mem), std::byte{0b0101'0101});
        state.ResumeTiming();

        std::size_t r = 0;

        auto end = mem.data() + mem.size();
        for (auto ptr = mem.data(); ptr < end; ptr += sizeof(std::size_t))
        {
            std::size_t val = 0;
            std::memcpy(&val, ptr, sizeof(std::size_t));

            int j = 1;
            for (;;)
            {
                int const zc = countr_zero(val);
                r += zc;
                j += zc;
                if (j < std::numeric_limits<std::size_t>::digits)
                {
                    break;
                }
                val >>= zc;

                int const oc = countr_one(val);
                j += oc;
                if (j < std::numeric_limits<std::size_t>::digits)
                {
                    break;
                }
                val >>= oc;
            }
        }

        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(countspecial);

BENCHMARK_MAIN();

#pragma once

#include <cstdint>

#include <limits>

#include <vefs/blob.hpp>

namespace vefs::utils
{
    // adapted from http://xoroshiro.di.unimi.it/splitmix64.c
    class splitmix64
    {
    public:
        using result_type = std::uint64_t;

        splitmix64() = delete;
        constexpr splitmix64(std::uint64_t init)
            : s(init)
        {
        }
        template<class Sseq>
        inline splitmix64(Sseq &init)
        {
            seed<Sseq>(init);
        }
        splitmix64(const splitmix64 &) = default;

        inline void seed(std::uint64_t init)
        {
            s = init;
        }
        template<class Sseq>
        inline void seed(Sseq &init)
        {
            (void)init;
            std::uint32_t *temp = reinterpret_cast<std::uint32_t *>(&s);
            init.generate(temp, temp + (sizeof(s) / sizeof(std::uint32_t)));
        }

        inline result_type operator()()
        {
            std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        inline void discard(unsigned long long j)
        {
            for (unsigned long long i = 0; i < j; ++i)
            {
                s += 0x9E3779B97F4A7C15ull;
            }
        }

        static constexpr result_type min()
        {
            return std::numeric_limits<result_type>::min();
        }
        static constexpr result_type max()
        {
            return std::numeric_limits<result_type>::max();
        }


    private:
        std::uint64_t s;
    };

    // adapted from http://xoroshiro.di.unimi.it/xoroshiro128plus.c
    class xoroshiro128plus
    {
    public:
        using result_type = std::uint64_t;

        xoroshiro128plus() = delete;
        inline xoroshiro128plus(std::uint64_t init)
        {
            seed(init);
        }
        template<class Sseq>
        inline xoroshiro128plus(Sseq &init)
        {
            seed<Sseq>(init);
        }
        constexpr xoroshiro128plus(std::uint64_t s1, std::uint64_t s2)
            : s{ s1, s2 }
        {
        }
        xoroshiro128plus(const xoroshiro128plus &) = default;

        inline void seed(std::uint64_t init)
        {
            // as advised by http://xoroshiro.di.unimi.it/xoroshiro128plus.c
            splitmix64 spreader(init);
            s[0] = spreader();
            s[1] = spreader();
        }
        template<class Sseq>
        inline void seed(Sseq &init)
        {
            (void)init;
            std::uint32_t *temp = reinterpret_cast<std::uint32_t *>(&s);
            init.generate(temp, temp + (sizeof(s) / sizeof(std::uint32_t)));
        }
        inline void seed(std::uint64_t s1, std::uint64_t s2)
        {
            s[0] = s1;
            s[1] = s2;
        }

        inline result_type operator()()
        {
            const uint64_t s0 = s[0];
            uint64_t s1 = s[1];
            const uint64_t result = s0 + s1;

            s1 ^= s0;
            s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
            s[1] = rotl(s1, 36); // c

            return result;
        }

        void fill(blob buffer)
        {
            while (buffer.size() >= sizeof(result_type))
            {
                buffer.pop_front_as<result_type>() = (*this)();
            }
            if (buffer.size())
            {
                auto tmp = (*this)();
                as_blob_view(tmp).copy_to(buffer);
            }
        }

        void discard(unsigned long long j)
        {
            for (unsigned long long i = 0; i < j; ++i)
            {
                const uint64_t s0 = s[0];
                uint64_t s1 = s[1];

                s1 ^= s0;
                s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
                s[1] = rotl(s1, 36); // c
            }
        }

        static constexpr result_type min()
        {
            return std::numeric_limits<result_type>::min();
        }
        static constexpr result_type max()
        {
            return std::numeric_limits<result_type>::max();
        }

    private:
        static inline uint64_t rotl(const uint64_t x, int k)
        {
            return (x << k) | (x >> (64 - k));
        }

        std::uint64_t s[2];
    };
}
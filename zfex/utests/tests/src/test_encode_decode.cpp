#include "zfex.h"
#include "zfex_macros.h"

#include "boost/ut.hpp"
#include "rapidcheck.h"

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <numeric>
#include <random>
#include <algorithm>


using namespace boost::ut;


template<typename T>
T *aligned_ptr(T *p)
{
    return (T *)(((uintptr_t)p + ZFEX_SIMD_ALIGNMENT - 1) & ~(ZFEX_SIMD_ALIGNMENT - 1));
};


suite EncodeDecode = []
{


"zfex fec_decode decodes data encoded with fec_encode"_test = []
{
    auto ok = rc::check(
        []()
        {
            std::uint32_t const SEED = *rc::gen::arbitrary<std::uint32_t>();
            std::uint32_t const m = *rc::gen::inRange<std::uint32_t>(2, 256);
            std::uint32_t const k = *rc::gen::inRange<std::uint32_t>(1, m + 1);

            // let's be reasonable about block size
            std::uint32_t const block_size = *rc::gen::inRange<std::uint32_t>(1, 100'001);

            std::vector<std::vector<std::uint8_t>> blocks(m);
            for (auto ix = 0u; ix < k; ++ix)
            {
                // [0:k] inblocks - random
                blocks[ix] = *rc::gen::container<std::vector<std::uint8_t>>(block_size, rc::gen::arbitrary<std::uint8_t>());
            }
            for (auto ix = k; ix < m; ++ix)
            {
                // [k:m] outblocks - empty
                blocks[ix].resize(block_size);
            }

            // prepare arrays of pointers for encoding
            std::vector<std::uint8_t *> block_ptrs(m);
            std::transform(blocks.begin(), blocks.end(), block_ptrs.begin(), [](auto & v){ return v.data(); });

            // prepare block indices
            std::vector<unsigned int> block_nums(m);
            std::iota(block_nums.begin(), block_nums.end(), 0);

            fec_t *fec_p = NULL;
            auto const sc = fec_new(k, m, &fec_p);
            RC_ASSERT(sc == ZFEX_SC_OK);

            /*
             * For encoding:
             *  input block pointers are block_ptrs[0:k]
             *  output block pointers are block_ptrs[k:m]
             *  output block indices are block_nums[k:m]
             */
            fec_encode(fec_p, block_ptrs.data(), block_ptrs.data() + k, block_nums.data() + k, m - k, block_size);

            /*
             * For decoding:
             *  1. I will shuffle indices in block_nums. 'Good' blocks indices will be assumed to be in [0:k]
             *  2. I will copy pointers to a new vector in order that follows shuffled indices
             */

            std::mt19937 g(SEED);
            std::shuffle(block_nums.begin(), block_nums.end(), g);

            std::vector<std::uint8_t const *> dx_iblock_ptrs(k);
            for (auto ix = 0u; ix < k; ++ix)
            {
                dx_iblock_ptrs[ix] = block_ptrs[block_nums[ix]];
            }

            // retrieve indices of blocks that will be recovered and placed into
            // decoder's output
            std::vector<unsigned int> to_recover;
            for (auto ix = k; ix < m; ++ix)
            {
                if (block_nums[ix] < k)
                {
                    to_recover.push_back(block_nums[ix]);
                }
            }
            // to_recover indices must be sorted
            std::sort(to_recover.begin(), to_recover.end());

            std::vector<std::uint8_t *> dx_oblock_ptrs;

            // Allocate placeholders for recovered blocks
            std::vector<std::vector<std::uint8_t>> dx_blocks(to_recover.size());
            for (auto & b : dx_blocks)
            {
                b.resize(block_size);

                // dx_block_ptrs[k:] will contain pointers to recovery blocks
                dx_oblock_ptrs.push_back(b.data());
            }

            fec_decode(fec_p, dx_iblock_ptrs.data(), dx_oblock_ptrs.data(), block_nums.data(), block_size);

            fec_free(fec_p);

            for (auto ix = 0u; ix < to_recover.size(); ++ix)
            {
                RC_ASSERT(dx_blocks[ix] == blocks[to_recover[ix]]);
            }
        }
    );

    expect(that % true == ok);
};


"zfex fec_decode decodes data encoded with fec_encode_simd"_test = []
{
    auto ok = rc::check(
        []()
        {

            std::uint32_t const SEED = *rc::gen::arbitrary<std::uint32_t>();
            std::uint32_t const m = *rc::gen::inRange<std::uint32_t>(2, 256);
            std::uint32_t const k = *rc::gen::inRange<std::uint32_t>(1, m + 1);

            // let's be reasonable about block size
            std::uint32_t const block_size = *rc::gen::inRange<std::uint32_t>(1, 100'001);
            std::uint32_t const aligned_block_size = block_size + ZFEX_SIMD_ALIGNMENT - 1; // this is a 'safe' block size with alignment taken into account

            std::vector<std::vector<std::uint8_t>> blocks(m);
            for (auto ix = 0u; ix < k; ++ix)
            {
                // [0:k] inblocks - random
                auto const b = *rc::gen::container<std::vector<std::uint8_t>>(block_size, rc::gen::arbitrary<std::uint8_t>());

                blocks[ix].resize(aligned_block_size);
                // copy randomized vector into aligned space
                std::copy(b.cbegin(), b.cend(), aligned_ptr(blocks[ix].data()));
            }
            for (auto ix = k; ix < m; ++ix)
            {
                // [k:m] outblocks - empty
                blocks[ix].resize(aligned_block_size);
            }

            // prepare arrays of aligned pointers for encoding
            std::vector<std::uint8_t *> block_ptrs(m);
            std::transform(blocks.begin(), blocks.end(), block_ptrs.begin(), [](auto & v){ return aligned_ptr(v.data()); });

            // prepare block indices
            std::vector<unsigned int> block_nums(m);
            std::iota(block_nums.begin(), block_nums.end(), 0);

            fec_t *fec_p = NULL;
            auto const sc = fec_new(k, m, &fec_p);
            RC_ASSERT(sc == ZFEX_SC_OK);

            /*
             * For encoding:
             *  input block pointers are block_ptrs[0:k]
             *  output block pointers are block_ptrs[k:m]
             *  output block indices are block_nums[k:m]
             */
            fec_encode_simd(fec_p, block_ptrs.data(), block_ptrs.data() + k, block_nums.data() + k, m - k, block_size);

            /*
             * For decoding:
             *  1. I will shuffle indices in block_nums. 'Good' blocks indices will be assumed to be in [0:k]
             *  2. I will copy pointers to a new vector in order that follows shuffled indices
             */

            std::mt19937 g(SEED);
            std::shuffle(block_nums.begin(), block_nums.end(), g);

            std::vector<std::uint8_t const *> dx_iblock_ptrs(k);
            for (auto ix = 0u; ix < k; ++ix)
            {
                dx_iblock_ptrs[ix] = block_ptrs[block_nums[ix]];
            }

            // retrieve indices of blocks that will be recovered and placed into
            // decoder's output
            std::vector<unsigned int> to_recover;
            for (auto ix = k; ix < m; ++ix)
            {
                if (block_nums[ix] < k)
                {
                    to_recover.push_back(block_nums[ix]);
                }
            }
            // to_recover indices must be sorted
            std::sort(to_recover.begin(), to_recover.end());

            std::vector<std::uint8_t *> dx_oblock_ptrs;

            // Allocate placeholders for recovered blocks
            std::vector<std::vector<std::uint8_t>> dx_blocks(to_recover.size());
            for (auto & b : dx_blocks)
            {
                b.resize(block_size);

                // dx_block_ptrs[k:] will contain pointers to recovery blocks
                dx_oblock_ptrs.push_back(b.data());
            }

            fec_decode(fec_p, dx_iblock_ptrs.data(), dx_oblock_ptrs.data(), block_nums.data(), block_size);

            fec_free(fec_p);

            for (auto ix = 0u; ix < to_recover.size(); ++ix)
            {
                // test is against data in block_ptrs, which holds aligned pointers used with fec_encode_simd
                RC_ASSERT(std::equal(dx_blocks[ix].cbegin(), dx_blocks[ix].cend(), block_ptrs[to_recover[ix]]));
            }
        }
    );

    expect(that % true == ok);
};


};


int main()
{
    auto failed = cfg<>.run({.report_errors = true});

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

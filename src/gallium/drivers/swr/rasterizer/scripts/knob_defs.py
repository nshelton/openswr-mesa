﻿# Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Python source
KNOBS = [
    ['ENABLE_ASSERT_DIALOGS', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Use dialogs when asserts fire.',
                       'Asserts are only enabled in debug builds'],
    }],

    ['USE_GENERIC_STORETILE', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Always use generic function for performing StoreTile.',
                       'Will be slightly slower than using optimized (jitted) path'],
    }],

    ['SINGLE_THREADED', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['If enabled will perform all rendering on the API thread.',
                       'This is useful mainly for debugging purposes.'],
    }],

    ['FAST_CLEAR', {
        'type'      : 'bool',
        'default'   : 'true',
        'desc'      : ['Replace 3D primitive execute with a SWRClearRT operation and',
                       'defer clear execution to first backend op on hottile, or hottile store'],
    }],

    ['MAX_NUMA_NODES', {
        'type'      : 'uint32_t',
        'default'   : '0',
        'desc'      : ['Maximum # of NUMA-nodes per system used for worker threads',
                       '  0 == ALL NUMA-nodes in the system',
                       '  N == Use at most N NUMA-nodes for rendering'],
    }],

    ['MAX_CORES_PER_NUMA_NODE', {
        'type'      : 'uint32_t',
        'default'   : '0',
        'desc'      : ['Maximum # of cores per NUMA-node used for worker threads.',
                       '  0 == ALL non-API thread cores per NUMA-node',
                       '  N == Use at most N cores per NUMA-node'],
    }],

    ['MAX_THREADS_PER_CORE', {
        'type'      : 'uint32_t',
        'default'   : '1',
        'desc'      : ['Maximum # of (hyper)threads per physical core used for worker threads.',
                       '  0 == ALL hyper-threads per core',
                       '  N == Use at most N hyper-threads per physical core'],
    }],

    ['BUCKETS_START_FRAME', {
        'type'      : 'uint32_t',
        'default'   : '1200',
        'desc'      : ['Frame from when to start saving buckets data.',
                       '',
                       'NOTE: KNOB_ENABLE_RDTSC must be enabled in core/knobs.h',
                       'for this to have an effect.'],
    }],

    ['BUCKETS_END_FRAME', {
        'type'      : 'uint32_t',
        'default'   : '1400',
        'desc'      : ['Frame at which to stop saving buckets data.',
                       '',
                       'NOTE: KNOB_ENABLE_RDTSC must be enabled in core/knobs.h',
                       'for this to have an effect.'],
    }],

    ['BUCKETS_ENABLE_THREADVIZ', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Enable threadviz output.'],
    }],

    ['TOSS_DRAW', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Disable per-draw execution',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['TOSS_QUEUE_FE', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at worker FE',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['TOSS_FETCH', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at vertex fetch',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['TOSS_IA', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at input assembler',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['TOSS_VS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at vertex shader',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['TOSS_SETUP_TRIS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at primitive setup',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['TOSS_BIN_TRIS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at primitive binning',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['TOSS_RS', {
        'type'      : 'bool',
        'default'   : 'false',
        'desc'      : ['Stop per-draw execution at rasterizer',
                       '',
                       'NOTE: Requires KNOB_ENABLE_TOSS_POINTS to be enabled in core/knobs.h'],
    }],

    ['WORKER_SPIN_LOOP_COUNT', {
        'type'      : 'uint32_t',
        'default'   : '5000',
        'desc'      : ['Number of spin-loop iterations worker threads will perform',
                       'before going to sleep when waiting for work'],
    }],

    ['MAX_DRAWS_IN_FLIGHT', {
        'type'      : 'uint32_t',
        'default'   : '160',
        'desc'      : ['Maximum number of draws outstanding before API thread blocks.'],
    }],

    ['MAX_PRIMS_PER_DRAW', {
       'type'       : 'uint32_t',
       'default'    : '2040',
       'desc'       : ['Maximum primitives in a single Draw().',
                       'Larger primitives are split into smaller Draw calls.',
                       'Should be a multiple of (3 * vectorWidth).'],
    }],

    ['MAX_TESS_PRIMS_PER_DRAW', {
       'type'       : 'uint32_t',
       'default'    : '16',
       'desc'       : ['Maximum primitives in a single Draw() with tessellation enabled.',
                       'Larger primitives are split into smaller Draw calls.',
                       'Should be a multiple of (vectorWidth).'],
    }],

    ['MAX_FRAC_ODD_TESS_FACTOR', {
        'type'      : 'float',
        'default'   : '63.0f',
        'desc'      : ['(DEBUG) Maximum tessellation factor for fractional-odd partitioning.'],
    }],                

    ['MAX_FRAC_EVEN_TESS_FACTOR', {
        'type'      : 'float',
        'default'   : '64.0f',
        'desc'      : ['(DEBUG) Maximum tessellation factor for fractional-even partitioning.'],
    }],                

    ['MAX_INTEGER_TESS_FACTOR', {
        'type'      : 'uint32_t',
        'default'   : '64',
        'desc'      : ['(DEBUG) Maximum tessellation factor for integer partitioning.'],
    }],                

    ['DUMP_SHADER_IR', {
       'type'       : 'bool',
       'default'    : 'false',
       'desc'       : ['Dumps shader LLVM IR at various stages of jit compilation.'],
    }],


]

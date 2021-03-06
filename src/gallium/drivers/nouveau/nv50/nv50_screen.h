#ifndef __NV50_SCREEN_H__
#define __NV50_SCREEN_H__

#include "nouveau_screen.h"
#include "nouveau_fence.h"
#include "nouveau_mm.h"
#include "nouveau_heap.h"

#include "nv50/nv50_winsys.h"
#include "nv50/nv50_stateobj.h"

#define NV50_TIC_MAX_ENTRIES 2048
#define NV50_TSC_MAX_ENTRIES 2048

/* doesn't count reserved slots (for auxiliary constants, immediates, etc.) */
#define NV50_MAX_PIPE_CONSTBUFS 14

struct nv50_context;

#define NV50_CODE_BO_SIZE_LOG2 19

#define NV50_SCREEN_RESIDENT_BO_COUNT 5

#define NV50_MAX_VIEWPORTS 16

struct nv50_blitter;

struct nv50_graph_state {
   uint32_t instance_elts; /* bitmask of per-instance elements */
   uint32_t instance_base;
   uint32_t interpolant_ctrl;
   uint32_t semantic_color;
   uint32_t semantic_psize;
   int32_t index_bias;
   bool uniform_buffer_bound[3];
   bool prim_restart;
   bool point_sprite;
   bool rt_serialize;
   bool flushed;
   bool rasterizer_discard;
   uint8_t tls_required;
   bool new_tls_space;
   uint8_t num_vtxbufs;
   uint8_t num_vtxelts;
   uint8_t num_textures[3];
   uint8_t num_samplers[3];
   uint8_t prim_size;
   uint16_t scissor;
};

struct nv50_screen {
   struct nouveau_screen base;

   struct nv50_context *cur_ctx;
   struct nv50_graph_state save_state;

   int num_occlusion_queries_active;

   struct nouveau_bo *code;
   struct nouveau_bo *uniforms;
   struct nouveau_bo *txc; /* TIC (offset 0) and TSC (65536) */
   struct nouveau_bo *stack_bo;
   struct nouveau_bo *tls_bo;

   unsigned TPs;
   unsigned MPsInTP;
   unsigned max_tls_space;
   unsigned cur_tls_space;

   struct nouveau_heap *vp_code_heap;
   struct nouveau_heap *gp_code_heap;
   struct nouveau_heap *fp_code_heap;

   struct nv50_blitter *blitter;

   struct {
      void **entries;
      int next;
      uint32_t lock[NV50_TIC_MAX_ENTRIES / 32];
   } tic;

   struct {
      void **entries;
      int next;
      uint32_t lock[NV50_TSC_MAX_ENTRIES / 32];
   } tsc;

   struct {
      uint32_t *map;
      struct nouveau_bo *bo;
   } fence;

   struct nouveau_object *sync;

   struct nouveau_object *tesla;
   struct nouveau_object *eng2d;
   struct nouveau_object *m2mf;
};

static inline struct nv50_screen *
nv50_screen(struct pipe_screen *screen)
{
   return (struct nv50_screen *)screen;
}

bool nv50_blitter_create(struct nv50_screen *);
void nv50_blitter_destroy(struct nv50_screen *);

int nv50_screen_tic_alloc(struct nv50_screen *, void *);
int nv50_screen_tsc_alloc(struct nv50_screen *, void *);

static inline void
nv50_resource_fence(struct nv04_resource *res, uint32_t flags)
{
   struct nv50_screen *screen = nv50_screen(res->base.screen);

   if (res->mm) {
      nouveau_fence_ref(screen->base.fence.current, &res->fence);
      if (flags & NOUVEAU_BO_WR)
         nouveau_fence_ref(screen->base.fence.current, &res->fence_wr);
   }
}

static inline void
nv50_resource_validate(struct nv04_resource *res, uint32_t flags)
{
   if (likely(res->bo)) {
      if (flags & NOUVEAU_BO_WR)
         res->status |= NOUVEAU_BUFFER_STATUS_GPU_WRITING |
            NOUVEAU_BUFFER_STATUS_DIRTY;
      if (flags & NOUVEAU_BO_RD)
         res->status |= NOUVEAU_BUFFER_STATUS_GPU_READING;

      nv50_resource_fence(res, flags);
   }
}

struct nv50_format {
   uint32_t rt;
   uint32_t tic;
   uint32_t vtx;
   uint32_t usage;
};

extern const struct nv50_format nv50_format_table[];

static inline void
nv50_screen_tic_unlock(struct nv50_screen *screen, struct nv50_tic_entry *tic)
{
   if (tic->id >= 0)
      screen->tic.lock[tic->id / 32] &= ~(1 << (tic->id % 32));
}

static inline void
nv50_screen_tsc_unlock(struct nv50_screen *screen, struct nv50_tsc_entry *tsc)
{
   if (tsc->id >= 0)
      screen->tsc.lock[tsc->id / 32] &= ~(1 << (tsc->id % 32));
}

static inline void
nv50_screen_tic_free(struct nv50_screen *screen, struct nv50_tic_entry *tic)
{
   if (tic->id >= 0) {
      screen->tic.entries[tic->id] = NULL;
      screen->tic.lock[tic->id / 32] &= ~(1 << (tic->id % 32));
   }
}

static inline void
nv50_screen_tsc_free(struct nv50_screen *screen, struct nv50_tsc_entry *tsc)
{
   if (tsc->id >= 0) {
      screen->tsc.entries[tsc->id] = NULL;
      screen->tsc.lock[tsc->id / 32] &= ~(1 << (tsc->id % 32));
   }
}

extern int nv50_tls_realloc(struct nv50_screen *screen, unsigned tls_space);

#endif

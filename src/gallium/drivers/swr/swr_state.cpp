/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#include "common/os.h"
#include "jit_api.h"
#include "JitManager.h"
#include "state_llvm.h"

#include "gallivm/lp_bld_tgsi.h"
#include "util/u_format.h"

#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_helpers.h"
#include "util/u_framebuffer.h"

#include "swr_state.h"
#include "swr_context.h"
#include "swr_context_llvm.h"
#include "swr_screen.h"
#include "swr_resource.h"
#include "swr_tex_sample.h"
#include "swr_scratch.h"
#include "swr_shader.h"

/* These should be pulled out into separate files as necessary
 * Just initializing everything here to get going. */

static void *
swr_create_blend_state(struct pipe_context *pipe,
                       const struct pipe_blend_state *blend)
{
   struct swr_blend_state *state = CALLOC_STRUCT(swr_blend_state);

   memcpy(&state->pipe, blend, sizeof(*blend));

   struct pipe_blend_state *pipe_blend = &state->pipe;

   for (int target = 0;
        target < std::min(SWR_NUM_RENDERTARGETS, PIPE_MAX_COLOR_BUFS);
        target++) {

      struct pipe_rt_blend_state *rt_blend = &pipe_blend->rt[target];
      SWR_RENDER_TARGET_BLEND_STATE &blendState =
         state->blendState.renderTarget[target];
      RENDER_TARGET_BLEND_COMPILE_STATE &compileState =
         state->compileState[target];

      if (target != 0 && !pipe_blend->independent_blend_enable) {
         memcpy(&compileState,
                &state->compileState[0],
                sizeof(RENDER_TARGET_BLEND_COMPILE_STATE));
         continue;
      }

      compileState.blendEnable = rt_blend->blend_enable;
      if (compileState.blendEnable) {
         compileState.sourceAlphaBlendFactor =
            swr_convert_blend_factor(rt_blend->alpha_src_factor);
         compileState.destAlphaBlendFactor =
            swr_convert_blend_factor(rt_blend->alpha_dst_factor);
         compileState.sourceBlendFactor =
            swr_convert_blend_factor(rt_blend->rgb_src_factor);
         compileState.destBlendFactor =
            swr_convert_blend_factor(rt_blend->rgb_dst_factor);

         compileState.colorBlendFunc =
            swr_convert_blend_func(rt_blend->rgb_func);
         compileState.alphaBlendFunc =
            swr_convert_blend_func(rt_blend->alpha_func);
      }

      blendState.writeDisableRed =
         (rt_blend->colormask & PIPE_MASK_R) ? 0 : 1;
      blendState.writeDisableGreen =
         (rt_blend->colormask & PIPE_MASK_G) ? 0 : 1;
      blendState.writeDisableBlue =
         (rt_blend->colormask & PIPE_MASK_B) ? 0 : 1;
      blendState.writeDisableAlpha =
         (rt_blend->colormask & PIPE_MASK_A) ? 0 : 1;
   }

   return state;
}

static void
swr_bind_blend_state(struct pipe_context *pipe, void *blend)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->blend == blend)
      return;

   ctx->blend = (swr_blend_state *)blend;

   ctx->dirty |= SWR_NEW_BLEND;
}

static void
swr_delete_blend_state(struct pipe_context *pipe, void *blend)
{
   FREE(blend);
}

static void
swr_set_blend_color(struct pipe_context *pipe,
                    const struct pipe_blend_color *color)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->blend_color = *color;

   ctx->dirty |= SWR_NEW_BLEND;
}

static void
swr_set_stencil_ref(struct pipe_context *pipe,
                    const struct pipe_stencil_ref *ref)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->stencil_ref = *ref;

   ctx->dirty |= SWR_NEW_DEPTH_STENCIL_ALPHA;
}

static void *
swr_create_depth_stencil_state(
   struct pipe_context *pipe,
   const struct pipe_depth_stencil_alpha_state *depth_stencil)
{
   struct pipe_depth_stencil_alpha_state *state;

   state = (pipe_depth_stencil_alpha_state *)mem_dup(depth_stencil,
                                                     sizeof *depth_stencil);

   return state;
}

static void
swr_bind_depth_stencil_state(struct pipe_context *pipe, void *depth_stencil)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->depth_stencil == (pipe_depth_stencil_alpha_state *)depth_stencil)
      return;

   ctx->depth_stencil = (pipe_depth_stencil_alpha_state *)depth_stencil;

   ctx->dirty |= SWR_NEW_DEPTH_STENCIL_ALPHA;
}

static void
swr_delete_depth_stencil_state(struct pipe_context *pipe, void *depth)
{
   FREE(depth);
}


static void *
swr_create_rasterizer_state(struct pipe_context *pipe,
                            const struct pipe_rasterizer_state *rast)
{
   struct pipe_rasterizer_state *state;
   state = (pipe_rasterizer_state *)mem_dup(rast, sizeof *rast);

   return state;
}

static void
swr_bind_rasterizer_state(struct pipe_context *pipe, void *handle)
{
   struct swr_context *ctx = swr_context(pipe);
   const struct pipe_rasterizer_state *rasterizer =
      (const struct pipe_rasterizer_state *)handle;

   if (ctx->rasterizer == (pipe_rasterizer_state *)rasterizer)
      return;

   ctx->rasterizer = (pipe_rasterizer_state *)rasterizer;

   ctx->dirty |= SWR_NEW_RASTERIZER;
}

static void
swr_delete_rasterizer_state(struct pipe_context *pipe, void *rasterizer)
{
   FREE(rasterizer);
}


static void *
swr_create_sampler_state(struct pipe_context *pipe,
                         const struct pipe_sampler_state *sampler)
{
   struct pipe_sampler_state *state =
      (pipe_sampler_state *)mem_dup(sampler, sizeof *sampler);

   return state;
}

static void
swr_bind_sampler_states(struct pipe_context *pipe,
                        unsigned shader,
                        unsigned start,
                        unsigned num,
                        void **samplers)
{
   struct swr_context *ctx = swr_context(pipe);
   unsigned i;

   assert(shader < PIPE_SHADER_TYPES);
   assert(start + num <= Elements(ctx->samplers[shader]));

   /* set the new samplers */
   ctx->num_samplers[shader] = num;
   for (i = 0; i < num; i++) {
      ctx->samplers[shader][start + i] = (pipe_sampler_state *)samplers[i];
   }

   ctx->dirty |= SWR_NEW_SAMPLER;
}

static void
swr_delete_sampler_state(struct pipe_context *pipe, void *sampler)
{
   FREE(sampler);
}


static struct pipe_sampler_view *
swr_create_sampler_view(struct pipe_context *pipe,
                        struct pipe_resource *texture,
                        const struct pipe_sampler_view *templ)
{
   struct pipe_sampler_view *view = CALLOC_STRUCT(pipe_sampler_view);

   if (view) {
      *view = *templ;
      view->reference.count = 1;
      view->texture = NULL;
      pipe_resource_reference(&view->texture, texture);
      view->context = pipe;
   }

   return view;
}

static void
swr_set_sampler_views(struct pipe_context *pipe,
                      unsigned shader,
                      unsigned start,
                      unsigned num,
                      struct pipe_sampler_view **views)
{
   struct swr_context *ctx = swr_context(pipe);
   uint i;

   assert(num <= PIPE_MAX_SHADER_SAMPLER_VIEWS);

   assert(shader < PIPE_SHADER_TYPES);
   assert(start + num <= Elements(ctx->sampler_views[shader]));

   /* set the new sampler views */
   ctx->num_sampler_views[shader] = num;
   for (i = 0; i < num; i++) {
      /* Note: we're using pipe_sampler_view_release() here to work around
       * a possible crash when the old view belongs to another context that
       * was already destroyed.
       */
      pipe_sampler_view_release(pipe, &ctx->sampler_views[shader][start + i]);
      pipe_sampler_view_reference(&ctx->sampler_views[shader][start + i],
                                  views[i]);
   }

   ctx->dirty |= SWR_NEW_SAMPLER_VIEW;
}

static void
swr_sampler_view_destroy(struct pipe_context *pipe,
                         struct pipe_sampler_view *view)
{
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static void *
swr_create_vs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *vs)
{
   struct swr_vertex_shader *swr_vs =
      (swr_vertex_shader *)CALLOC_STRUCT(swr_vertex_shader);
   if (!swr_vs)
      return NULL;

   swr_vs->pipe.tokens = tgsi_dup_tokens(vs->tokens);
   swr_vs->pipe.stream_output = vs->stream_output;

   lp_build_tgsi_info(vs->tokens, &swr_vs->info);

   swr_vs->func = swr_compile_vs(pipe, swr_vs);

   swr_vs->soState = {0};

   if (swr_vs->pipe.stream_output.num_outputs) {
      pipe_stream_output_info *stream_output = &swr_vs->pipe.stream_output;

      swr_vs->soState.soEnable = true;
      // soState.rasterizerDisable set on state dirty
      // soState.streamToRasterizer not used

      for (uint32_t i = 0; i < stream_output->num_outputs; i++) {
         swr_vs->soState.streamMasks[stream_output->output[i].stream] |=
            1 << (stream_output->output[i].register_index - 1);
      }
      for (uint32_t i = 0; i < MAX_SO_STREAMS; i++) {
        swr_vs->soState.streamNumEntries[i] =
             _mm_popcnt_u32(swr_vs->soState.streamMasks[i]);
       }
   }

   return swr_vs;
}

static void
swr_bind_vs_state(struct pipe_context *pipe, void *vs)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->vs == vs)
      return;

   ctx->vs = (swr_vertex_shader *)vs;
   ctx->dirty |= SWR_NEW_VS;
}

static void
swr_delete_vs_state(struct pipe_context *pipe, void *vs)
{
   struct swr_vertex_shader *swr_vs = (swr_vertex_shader *)vs;
   FREE((void *)swr_vs->pipe.tokens);
   FREE(vs);
}

static void *
swr_create_fs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *fs)
{
   struct swr_fragment_shader *swr_fs = new swr_fragment_shader;
   if (!swr_fs)
      return NULL;

   swr_fs->pipe.tokens = tgsi_dup_tokens(fs->tokens);

   lp_build_tgsi_info(fs->tokens, &swr_fs->info);

   return swr_fs;
}


static void
swr_bind_fs_state(struct pipe_context *pipe, void *fs)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->fs == fs)
      return;

   ctx->fs = (swr_fragment_shader *)fs;
   ctx->dirty |= SWR_NEW_FS;
}

static void
swr_delete_fs_state(struct pipe_context *pipe, void *fs)
{
   struct swr_fragment_shader *swr_fs = (swr_fragment_shader *)fs;
   FREE((void *)swr_fs->pipe.tokens);
   delete swr_fs;
}


static void
swr_set_constant_buffer(struct pipe_context *pipe,
                        uint shader,
                        uint index,
                        struct pipe_constant_buffer *cb)
{
   struct swr_context *ctx = swr_context(pipe);
   struct pipe_resource *constants = cb ? cb->buffer : NULL;

   assert(shader < PIPE_SHADER_TYPES);
   assert(index < Elements(ctx->constants[shader]));

   /* note: reference counting */
   util_copy_constant_buffer(&ctx->constants[shader][index], cb);

   if (shader == PIPE_SHADER_VERTEX || shader == PIPE_SHADER_GEOMETRY) {
      ctx->dirty |= SWR_NEW_VSCONSTANTS;
   } else if (shader == PIPE_SHADER_FRAGMENT) {
      ctx->dirty |= SWR_NEW_FSCONSTANTS;
   }

   if (cb && cb->user_buffer) {
      pipe_resource_reference(&constants, NULL);
   }
}


static void *
swr_create_vertex_elements_state(struct pipe_context *pipe,
                                 unsigned num_elements,
                                 const struct pipe_vertex_element *attribs)
{
   struct swr_vertex_element_state *velems;
   assert(num_elements <= PIPE_MAX_ATTRIBS);
   velems = CALLOC_STRUCT(swr_vertex_element_state);
   if (velems) {
      velems->fsState.numAttribs = num_elements;
      for (unsigned i = 0; i < num_elements; i++) {
         // XXX: we should do this keyed on the VS usage info

         const struct util_format_description *desc =
            util_format_description(attribs[i].src_format);

         velems->fsState.layout[i].AlignedByteOffset = attribs[i].src_offset;
         velems->fsState.layout[i].Format =
            mesa_to_swr_format(attribs[i].src_format);
         velems->fsState.layout[i].StreamIndex =
            attribs[i].vertex_buffer_index;
         velems->fsState.layout[i].InstanceEnable =
            attribs[i].instance_divisor != 0;
         velems->fsState.layout[i].ComponentControl0 =
            desc->channel[0].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store0;
         velems->fsState.layout[i].ComponentControl1 =
            desc->channel[1].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store0;
         velems->fsState.layout[i].ComponentControl2 =
            desc->channel[2].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store0;
         velems->fsState.layout[i].ComponentControl3 =
            desc->channel[3].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store1Fp;
         velems->fsState.layout[i].ComponentPacking = ComponentEnable::XYZW;
         velems->fsState.layout[i].InstanceDataStepRate =
            attribs[i].instance_divisor;

         /* Calculate the pitch of each stream */
         const SWR_FORMAT_INFO &swr_desc = GetFormatInfo(
            mesa_to_swr_format(attribs[i].src_format));
         velems->stream_pitch[attribs[i].vertex_buffer_index] += swr_desc.Bpp;
      }
   }

   return velems;
}

static void
swr_bind_vertex_elements_state(struct pipe_context *pipe, void *velems)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_vertex_element_state *swr_velems =
      (struct swr_vertex_element_state *)velems;

   ctx->velems = swr_velems;
   ctx->dirty |= SWR_NEW_VERTEX;
}

static void
swr_delete_vertex_elements_state(struct pipe_context *pipe, void *velems)
{
   /* XXX Need to destroy fetch shader? */
   FREE(velems);
}


static void
swr_set_vertex_buffers(struct pipe_context *pipe,
                       unsigned start_slot,
                       unsigned num_elements,
                       const struct pipe_vertex_buffer *buffers)
{
   struct swr_context *ctx = swr_context(pipe);

   assert(num_elements <= PIPE_MAX_ATTRIBS);

   util_set_vertex_buffers_count(ctx->vertex_buffer,
                                 &ctx->num_vertex_buffers,
                                 buffers,
                                 start_slot,
                                 num_elements);

   ctx->dirty |= SWR_NEW_VERTEX;
}


static void
swr_set_index_buffer(struct pipe_context *pipe,
                     const struct pipe_index_buffer *ib)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ib)
      memcpy(&ctx->index_buffer, ib, sizeof(ctx->index_buffer));
   else
      memset(&ctx->index_buffer, 0, sizeof(ctx->index_buffer));

   ctx->dirty |= SWR_NEW_VERTEX;
}

static void
swr_set_polygon_stipple(struct pipe_context *pipe,
                        const struct pipe_poly_stipple *stipple)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->poly_stipple = *stipple; /* struct copy */
   ctx->dirty |= SWR_NEW_STIPPLE;
}

static void
swr_set_clip_state(struct pipe_context *pipe,
                   const struct pipe_clip_state *clip)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->clip = *clip;
   /* XXX Unimplemented, but prevents crash */

   ctx->dirty |= SWR_NEW_CLIP;
}


static void
swr_set_scissor_states(struct pipe_context *pipe,
                       unsigned start_slot,
                       unsigned num_viewports,
                       const struct pipe_scissor_state *scissor)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->scissor = *scissor;
   ctx->dirty |= SWR_NEW_SCISSOR;
}

static void
swr_set_viewport_states(struct pipe_context *pipe,
                        unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *vpt)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->viewport = *vpt;
   ctx->dirty |= SWR_NEW_VIEWPORT;
}


static void
swr_set_framebuffer_state(struct pipe_context *pipe,
                          const struct pipe_framebuffer_state *fb)
{
   struct swr_context *ctx = swr_context(pipe);

   boolean changed = !util_framebuffer_state_equal(&ctx->framebuffer, fb);

   assert(fb->width <= KNOB_GUARDBAND_WIDTH);
   assert(fb->height <= KNOB_GUARDBAND_HEIGHT);

   if (changed) {
      unsigned i;
      for (i = 0; i < fb->nr_cbufs; ++i)
         pipe_surface_reference(&ctx->framebuffer.cbufs[i], fb->cbufs[i]);
      for (; i < ctx->framebuffer.nr_cbufs; ++i)
         pipe_surface_reference(&ctx->framebuffer.cbufs[i], NULL);

      ctx->framebuffer.nr_cbufs = fb->nr_cbufs;

      ctx->framebuffer.width = fb->width;
      ctx->framebuffer.height = fb->height;

      pipe_surface_reference(&ctx->framebuffer.zsbuf, fb->zsbuf);

      ctx->dirty |= SWR_NEW_FRAMEBUFFER;
   }
}


static void
swr_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask)
{
   struct swr_context *ctx = swr_context(pipe);

   if (sample_mask != ctx->sample_mask) {
      ctx->sample_mask = sample_mask;
      ctx->dirty |= SWR_NEW_RASTERIZER;
   }
}


void
swr_update_derived(struct swr_context *ctx,
                   const struct pipe_draw_info *p_draw_info)
{
   /* Any state that requires dirty flags to be re-triggered sets this mask */
   /* For example, user_buffer vertex and index buffers. */
   unsigned post_update_dirty_flags = 0;

   /* Render Targets */
   if (ctx->dirty & SWR_NEW_FRAMEBUFFER) {
      struct pipe_framebuffer_state *fb = &ctx->framebuffer;
      SWR_SURFACE_STATE *new_attachment[SWR_NUM_ATTACHMENTS] = {0};
      boolean changed, need_idle;
      UINT i;

      /* colorbuffer targets */
      if (fb->nr_cbufs)
         for (i = 0; i < fb->nr_cbufs; ++i)
            if (fb->cbufs[i]) {
               struct swr_resource *colorBuffer =
                  swr_resource(fb->cbufs[i]->texture);
               new_attachment[SWR_ATTACHMENT_COLOR0 + i] = &colorBuffer->swr;
            }

      /* depth/stencil target */
      if (fb->zsbuf) {
         struct swr_resource *depthStencilBuffer =
            swr_resource(fb->zsbuf->texture);
         if (depthStencilBuffer->has_depth) {
            new_attachment[SWR_ATTACHMENT_DEPTH] = &depthStencilBuffer->swr;

            if (depthStencilBuffer->has_stencil)
               new_attachment[SWR_ATTACHMENT_STENCIL] =
                  &depthStencilBuffer->secondary;

         } else if (depthStencilBuffer->has_stencil)
            new_attachment[SWR_ATTACHMENT_STENCIL] = &depthStencilBuffer->swr;
      }

      /* For each attachment that has changed, store tile contents to render
       * target */
      changed = FALSE;
      need_idle = FALSE;
      for (i = 0; i < SWR_NUM_ATTACHMENTS; i++) {
         if ((uintptr_t)ctx->current.attachment[i]
             ^ (uintptr_t)new_attachment[i]) {
            if (ctx->current.attachment[i]) {
               enum SWR_TILE_STATE post_state;
               post_state =
                  (new_attachment[i] ? SWR_TILE_INVALID : SWR_TILE_RESOLVED);
               swr_store_render_target(ctx, i, post_state);
               need_idle |= TRUE;
            }
            changed |= TRUE;
         }
      }

      /*
       * Attachments are live, don't update any until idle
       * (all StoreTiles, called by swr_store_render_targets, finish)
       */
      if (need_idle)
         SwrWaitForIdle(ctx->swrContext);

      if (changed) {
         /* Update actual SWR core attachments, or clear those no longer
          * attached */
         swr_draw_context *pDC =
            (swr_draw_context *)SwrGetPrivateContextState(ctx->swrContext);
         SWR_SURFACE_STATE *renderTargets = pDC->renderTargets;
         for (i = 0; i < SWR_NUM_ATTACHMENTS; i++) {
            if ((uintptr_t)ctx->current.attachment[i]
                ^ (uintptr_t)new_attachment[i]) {
               if (new_attachment[i]) {
                  renderTargets[i] = *new_attachment[i];
                  ctx->current.attachment[i] = new_attachment[i];
               } else {
                  renderTargets[i] = {0};
                  ctx->current.attachment[i] = nullptr;
               }
            }
         }

         /* rendertarget changes also necessitate updating other state */
         ctx->dirty |= SWR_NEW_BLEND | SWR_NEW_SAMPLER_VIEW | SWR_NEW_VS
            | SWR_NEW_FS | SWR_NEW_RASTERIZER | SWR_NEW_VIEWPORT
            | SWR_NEW_DEPTH_STENCIL_ALPHA;
      }
   }

   /* Raster state */
   if (ctx->dirty & (SWR_NEW_RASTERIZER | SWR_NEW_VS)) {
      SWR_RASTSTATE *rastState = &ctx->current.rastState;
      rastState->cullMode = swr_convert_cull_mode(ctx->rasterizer->cull_face);
      rastState->frontWinding = ctx->rasterizer->front_ccw
         ? SWR_FRONTWINDING_CCW
         : SWR_FRONTWINDING_CW;
      rastState->scissorEnable = ctx->rasterizer->scissor;
      rastState->pointSize = ctx->rasterizer->point_size > 0.0f
         ? ctx->rasterizer->point_size
         : 1.0f;
      rastState->lineWidth = ctx->rasterizer->line_width > 0.0f
         ? ctx->rasterizer->line_width
         : 1.0f;

      rastState->pointParam = ctx->rasterizer->point_size_per_vertex;

      rastState->pointSpriteEnable = ctx->rasterizer->sprite_coord_enable;
      rastState->pointSpriteTopOrigin =
         ctx->rasterizer->sprite_coord_mode == PIPE_SPRITE_COORD_UPPER_LEFT;

      /* XXX TODO: Add multisample */
      rastState->sampleCount = SWR_MULTISAMPLE_1X;

      bool do_offset = false;
      switch (ctx->rasterizer->fill_front) {
      case PIPE_POLYGON_MODE_FILL:
         do_offset = ctx->rasterizer->offset_tri;
         break;
      case PIPE_POLYGON_MODE_LINE:
         do_offset = ctx->rasterizer->offset_line;
         break;
      case PIPE_POLYGON_MODE_POINT:
         do_offset = ctx->rasterizer->offset_point;
         break;
      }

      if (do_offset) {
         rastState->depthBias = ctx->rasterizer->offset_units;
         rastState->slopeScaledDepthBias = ctx->rasterizer->offset_scale;
         rastState->depthBiasClamp = ctx->rasterizer->offset_clamp;
      } else {
         rastState->depthBias = 0;
         rastState->slopeScaledDepthBias = 0;
         rastState->depthBiasClamp = 0;
      }
      struct pipe_surface *zb = ctx->framebuffer.zsbuf;
      if (zb && swr_resource(zb->texture)->has_depth)
         rastState->depthFormat = swr_resource(zb->texture)->swr.format;

      rastState->depthClipEnable = ctx->rasterizer->depth_clip;

      SwrSetRastState(ctx->swrContext, rastState);
   }

   /* Scissor */
   if (ctx->dirty & SWR_NEW_SCISSOR) {
      BBOX bbox(ctx->scissor.miny, ctx->scissor.maxy,
                   ctx->scissor.minx, ctx->scissor.maxx);
      SwrSetScissorRects(ctx->swrContext, 1, &bbox);
   }

   /* Viewport */
   if (ctx->dirty & SWR_NEW_VIEWPORT) {
      pipe_viewport_state *state = &ctx->viewport;
      SWR_VIEWPORT *vp = &ctx->current.vp;
      SWR_VIEWPORT_MATRIX *vpm = &ctx->current.vpm;

      const float scale_x = fabs(state->scale[0]);
      const float scale_y = fabs(state->scale[1]);
      const float scale_z = fabs(state->scale[2]);

      vp->x = state->translate[0] - scale_x;
      vp->width = state->translate[0] + scale_x;
      vp->y = state->translate[1] - scale_y;
      vp->height = state->translate[1] + scale_y;
      if (ctx->rasterizer->clip_halfz == 0) {
         vp->minZ = state->translate[2] - scale_z;
         vp->maxZ = state->translate[2] + scale_z;
      } else {
         vp->minZ = state->translate[2];
         vp->maxZ = state->translate[2] + scale_z;
      }

      /* Flip viewport for all targets except samplable textures. */
      /* XXX This may not be sufficient for multiple rendertargets */
      struct pipe_surface *cb = ctx->framebuffer.cbufs[0];
      if (cb &&
          !(swr_resource(cb->texture)->base.bind & PIPE_BIND_SAMPLER_VIEW)) {
         /* Flip y and y-translate in the viewport matrix. */
         vpm->m00 = (vp->width - vp->x) / 2.0f;
         vpm->m11 = (vp->y - vp->height) / 2.0f;
         vpm->m22 = (vp->maxZ - vp->minZ) / 2.0f;
         vpm->m30 = vp->x + vpm->m00;
         vpm->m31 = vp->height + vpm->m11;
         vpm->m32 = vp->minZ + vpm->m22;
      } else {
         vpm->m00 = (vp->width - vp->x) / 2.0f;
         vpm->m11 = (vp->height - vp->y) / 2.0f;
         vpm->m22 = (vp->maxZ - vp->minZ) / 2.0f;
         vpm->m30 = vp->x + vpm->m00;
         vpm->m31 = vp->y + vpm->m11;
         vpm->m32 = vp->minZ + vpm->m22;
      }

      /* Now that the matrix is calculated, clip the view coords to screen
       * size.  OpenGL allows for -ve x,y in the viewport.
       */
      vp->x = std::max(vp->x, 0.0f);
      vp->y = std::max(vp->y, 0.0f);
      vp->width = std::min(vp->width, (float)ctx->framebuffer.width);
      vp->height = std::min(vp->height, (float)ctx->framebuffer.height);

      SwrSetViewports(ctx->swrContext, 1, vp, vpm);
   }

   /* Set vertex & index buffers */
   /* (using draw info if called by swr_draw_vbo) */
   if (ctx->dirty & SWR_NEW_VERTEX) {
      uint32_t size, pitch, max_vertex, partial_inbounds;
      const uint8_t *p_data;

      /* If being called by swr_draw_vbo, copy draw details */
      struct pipe_draw_info info = {0};
      if (p_draw_info)
         info = *p_draw_info;

      /* vertex buffers */
      SWR_VERTEX_BUFFER_STATE swrVertexBuffers[PIPE_MAX_ATTRIBS];
      for (UINT i = 0; i < ctx->num_vertex_buffers; i++) {
         pipe_vertex_buffer *vb = &ctx->vertex_buffer[i];

         pitch = vb->stride;
         if (!vb->user_buffer) {
            /* VBO
             * size is based on buffer->width0 rather than info.max_index
             * to prevent having to validate VBO on each draw */
            size = vb->buffer->width0;
            max_vertex = size / pitch;
            partial_inbounds = size % pitch;

            p_data = (const uint8_t *)swr_resource_data(vb->buffer)
               + vb->buffer_offset;
         } else {
            /* Client buffer
             * client memory is one-time use, re-trigger SWR_NEW_VERTEX to
             * revalidate on each draw */
            post_update_dirty_flags |= SWR_NEW_VERTEX;

            if (pitch) {
               size = (info.max_index - info.min_index + 1) * pitch;
            } else {
               /* pitch = 0, means constant value
                * set size to 1 vertex */
               size = ctx->velems->stream_pitch[i];
            }

            max_vertex = info.max_index + 1;
            partial_inbounds = 0;

            /* Copy only needed vertices to scratch space */
            size = AlignUp(size, 4);
            const void *ptr = (const uint8_t *) vb->user_buffer
               + info.min_index * pitch;
            ptr = swr_copy_to_scratch_space(
               ctx, &ctx->scratch->vertex_buffer, ptr, size);
            p_data = (const uint8_t *)ptr - info.min_index * pitch;
         }

         swrVertexBuffers[i] = {0};
         swrVertexBuffers[i].index = i;
         swrVertexBuffers[i].pitch = pitch;
         swrVertexBuffers[i].pData = p_data;
         swrVertexBuffers[i].size = size;
         swrVertexBuffers[i].maxVertex = max_vertex;
         swrVertexBuffers[i].partialInboundsSize = partial_inbounds;
      }

      SwrSetVertexBuffers(
         ctx->swrContext, ctx->num_vertex_buffers, swrVertexBuffers);

      /* index buffer, if required (info passed in by swr_draw_vbo) */
      SWR_FORMAT index_type = R32_UINT; /* Default for non-indexed draws */
      if (info.indexed) {
         pipe_index_buffer *ib = &ctx->index_buffer;

         pitch = ib->index_size ? ib->index_size : sizeof(uint32_t);
         index_type = swr_convert_index_type(pitch);

         if (!ib->user_buffer) {
            /* VBO
             * size is based on buffer->width0 rather than info.count
             * to prevent having to validate VBO on each draw */
            size = ib->buffer->width0;
            p_data =
               (const uint8_t *)swr_resource_data(ib->buffer) + ib->offset;
         } else {
            /* Client buffer
             * client memory is one-time use, re-trigger SWR_NEW_VERTEX to
             * revalidate on each draw */
            post_update_dirty_flags |= SWR_NEW_VERTEX;

            size = info.count * pitch;
            size = AlignUp(size, 4);

            /* Copy indices to scratch space */
            const void *ptr = ib->user_buffer;
            ptr = swr_copy_to_scratch_space(
               ctx, &ctx->scratch->index_buffer, ptr, size);
            p_data = (const uint8_t *)ptr;
         }

         SWR_INDEX_BUFFER_STATE swrIndexBuffer;
         swrIndexBuffer.format = swr_convert_index_type(ib->index_size);
         swrIndexBuffer.pIndices = p_data;
         swrIndexBuffer.size = size;

         SwrSetIndexBuffer(ctx->swrContext, &swrIndexBuffer);
      }

      struct swr_vertex_element_state *velems = ctx->velems;
      if (velems && velems->fsState.indexType != index_type) {
         velems->fsFunc = NULL;
         velems->fsState.indexType = index_type;
      }
   }

   /* VertexShader */
   if (ctx->dirty & SWR_NEW_VS) {
      SwrSetVertexFunc(ctx->swrContext, ctx->vs->func);
   }

   swr_jit_key key;
   if (ctx->dirty & (SWR_NEW_FS | SWR_NEW_SAMPLER | SWR_NEW_SAMPLER_VIEW
                     | SWR_NEW_RASTERIZER | SWR_NEW_FRAMEBUFFER)) {
      memset(&key, 0, sizeof(key));
      swr_generate_fs_key(key, ctx, ctx->fs);
      auto search = ctx->fs->map.find(key);
      PFN_PIXEL_KERNEL func;
      if (search != ctx->fs->map.end()) {
         func = search->second;
      } else {
         func = swr_compile_fs(ctx, key);
         ctx->fs->map.insert(std::make_pair(key, func));
      }
      SWR_PS_STATE psState = {0};
      psState.pfnPixelShader = func;
      psState.killsPixel = ctx->fs->info.base.uses_kill;
      psState.inputCoverage = SWR_INPUT_COVERAGE_NORMAL;
      psState.writesODepth = ctx->fs->info.base.writes_z;
      psState.usesSourceDepth = ctx->fs->info.base.reads_z;
      psState.maxRTSlotUsed =
         (ctx->framebuffer.nr_cbufs != 0) ?
         (ctx->framebuffer.nr_cbufs - 1) :
         0;
      SwrSetPixelShaderState(ctx->swrContext, &psState);
   }

   /* JIT sampler state */
   if (ctx->dirty & SWR_NEW_SAMPLER) {
      swr_draw_context *pDC =
         (swr_draw_context *)SwrGetPrivateContextState(ctx->swrContext);

      for (unsigned i = 0; i < key.nr_samplers; i++) {
         const struct pipe_sampler_state *sampler =
            ctx->samplers[PIPE_SHADER_FRAGMENT][i];

         if (sampler) {
            pDC->samplersFS[i].min_lod = sampler->min_lod;
            pDC->samplersFS[i].max_lod = sampler->max_lod;
            pDC->samplersFS[i].lod_bias = sampler->lod_bias;
            COPY_4V(pDC->samplersFS[i].border_color, sampler->border_color.f);
         }
      }
   }

   /* JIT sampler view state */
   if (ctx->dirty & SWR_NEW_SAMPLER_VIEW) {
      swr_draw_context *pDC =
         (swr_draw_context *)SwrGetPrivateContextState(ctx->swrContext);

      for (unsigned i = 0; i < key.nr_sampler_views; i++) {
         struct pipe_sampler_view *view =
            ctx->sampler_views[PIPE_SHADER_FRAGMENT][i];

         if (view) {
            struct pipe_resource *res = view->texture;
            struct swr_resource *swr_res = swr_resource(res);
            struct swr_jit_texture *jit_tex = &pDC->texturesFS[i];
            memset(jit_tex, 0, sizeof(*jit_tex));
            jit_tex->width = res->width0;
            jit_tex->height = res->height0;
            jit_tex->depth = res->depth0;
            jit_tex->first_level = view->u.tex.first_level;
            jit_tex->last_level = view->u.tex.last_level;
            jit_tex->base_ptr = swr_res->swr.pBaseAddress;

            for (unsigned level = jit_tex->first_level;
                 level <= jit_tex->last_level;
                 level++) {
               jit_tex->row_stride[level] = swr_res->row_stride[level];
               jit_tex->img_stride[level] = swr_res->img_stride[level];
               jit_tex->mip_offsets[level] = swr_res->mip_offsets[level];
            }
         }
      }
   }

   /* VertexShader Constants */
   if (ctx->dirty & SWR_NEW_VSCONSTANTS) {
      swr_draw_context *pDC =
         (swr_draw_context *)SwrGetPrivateContextState(ctx->swrContext);

      for (UINT i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++) {
         const pipe_constant_buffer *cb =
            &ctx->constants[PIPE_SHADER_VERTEX][i];
         pDC->num_constantsVS[i] = cb->buffer_size;
         if (cb->buffer)
            pDC->constantVS[i] =
               (const float *)((const BYTE *)cb->buffer + cb->buffer_offset);
         else {
            /* Need to copy these constants to scratch space */
            if (cb->user_buffer && cb->buffer_size) {
               const void *ptr =
                  ((const BYTE *)cb->user_buffer + cb->buffer_offset);
               uint32_t size = AlignUp(cb->buffer_size, 4);
               ptr = swr_copy_to_scratch_space(
                  ctx, &ctx->scratch->vs_constants, ptr, size);
               pDC->constantVS[i] = (const float *)ptr;
            }
         }
      }
   }

   /* FragmentShader Constants */
   if (ctx->dirty & SWR_NEW_FSCONSTANTS) {
      swr_draw_context *pDC =
         (swr_draw_context *)SwrGetPrivateContextState(ctx->swrContext);

      for (UINT i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++) {
         const pipe_constant_buffer *cb =
            &ctx->constants[PIPE_SHADER_FRAGMENT][i];
         pDC->num_constantsFS[i] = cb->buffer_size;
         if (cb->buffer)
            pDC->constantFS[i] =
               (const float *)((const BYTE *)cb->buffer + cb->buffer_offset);
         else {
            /* Need to copy these constants to scratch space */
            if (cb->user_buffer && cb->buffer_size) {
               const void *ptr =
                  ((const BYTE *)cb->user_buffer + cb->buffer_offset);
               uint32_t size = AlignUp(cb->buffer_size, 4);
               ptr = swr_copy_to_scratch_space(
                  ctx, &ctx->scratch->fs_constants, ptr, size);
               pDC->constantFS[i] = (const float *)ptr;
            }
         }
      }
   }

   /* Depth/stencil state */
   if (ctx->dirty & SWR_NEW_DEPTH_STENCIL_ALPHA) {
      struct pipe_depth_state *depth = &(ctx->depth_stencil->depth);
      struct pipe_stencil_state *stencil = ctx->depth_stencil->stencil;
      SWR_DEPTH_STENCIL_STATE depthStencilState = {{0}};

      /* XXX, incomplete.  Need to flesh out stencil & alpha test state
      struct pipe_stencil_state *front_stencil =
      ctx->depth_stencil.stencil[0];
      struct pipe_stencil_state *back_stencil = ctx->depth_stencil.stencil[1];
      struct pipe_alpha_state alpha;
      */
      if (stencil[0].enabled) {
         depthStencilState.stencilWriteEnable = 1;
         depthStencilState.stencilTestEnable = 1;
         depthStencilState.stencilTestFunc =
            swr_convert_depth_func(stencil[0].func);

         depthStencilState.stencilPassDepthPassOp =
            swr_convert_stencil_op(stencil[0].zpass_op);
         depthStencilState.stencilPassDepthFailOp =
            swr_convert_stencil_op(stencil[0].zfail_op);
         depthStencilState.stencilFailOp =
            swr_convert_stencil_op(stencil[0].fail_op);
         depthStencilState.stencilWriteMask = stencil[0].writemask;
         depthStencilState.stencilTestMask = stencil[0].valuemask;
         depthStencilState.stencilRefValue = ctx->stencil_ref.ref_value[0];
      }
      if (stencil[1].enabled) {
         depthStencilState.doubleSidedStencilTestEnable = 1;

         depthStencilState.backfaceStencilTestFunc =
            swr_convert_depth_func(stencil[1].func);

         depthStencilState.backfaceStencilPassDepthPassOp =
            swr_convert_stencil_op(stencil[1].zpass_op);
         depthStencilState.backfaceStencilPassDepthFailOp =
            swr_convert_stencil_op(stencil[1].zfail_op);
         depthStencilState.backfaceStencilFailOp =
            swr_convert_stencil_op(stencil[1].fail_op);
         depthStencilState.backfaceStencilWriteMask = stencil[1].writemask;
         depthStencilState.backfaceStencilTestMask = stencil[1].valuemask;

         depthStencilState.backfaceStencilRefValue =
            ctx->stencil_ref.ref_value[1];
      }

      depthStencilState.depthTestEnable = depth->enabled;
      depthStencilState.depthTestFunc = swr_convert_depth_func(depth->func);
      depthStencilState.depthWriteEnable = depth->writemask;
      SwrSetDepthStencilState(ctx->swrContext, &depthStencilState);
   }

   /* Blend State */
   if (ctx->dirty & (SWR_NEW_BLEND |
                     SWR_NEW_FRAMEBUFFER |
                     SWR_NEW_DEPTH_STENCIL_ALPHA)) {
      struct pipe_framebuffer_state *fb = &ctx->framebuffer;

      SWR_BLEND_STATE blendState;
      memcpy(&blendState, &ctx->blend->blendState, sizeof(blendState));
      blendState.constantColor[0] = ctx->blend_color.color[0];
      blendState.constantColor[1] = ctx->blend_color.color[1];
      blendState.constantColor[2] = ctx->blend_color.color[2];
      blendState.constantColor[3] = ctx->blend_color.color[3];
      blendState.alphaTestReference =
         *((uint32_t*)&ctx->depth_stencil->alpha.ref_value);

      /* If there are no color buffers bound, disable writes on RT0
       * and skip loop */
      if (fb->nr_cbufs == 0) {
         blendState.renderTarget[0].writeDisableRed = 1;
         blendState.renderTarget[0].writeDisableGreen = 1;
         blendState.renderTarget[0].writeDisableBlue = 1;
         blendState.renderTarget[0].writeDisableAlpha = 1;
      }
      else
         for (int target = 0;
               target < std::min(SWR_NUM_RENDERTARGETS,
                                 PIPE_MAX_COLOR_BUFS);
               target++) {
            if (!fb->cbufs[target])
               continue;

            struct swr_resource *colorBuffer =
               swr_resource(fb->cbufs[target]->texture);

            BLEND_COMPILE_STATE compileState;
            memset(&compileState, 0, sizeof(compileState));
            compileState.format = colorBuffer->swr.format;
            memcpy(&compileState.blendState,
                   &ctx->blend->compileState[target],
                   sizeof(compileState.blendState));

            compileState.desc.alphaTestEnable =
               ctx->depth_stencil->alpha.enabled;
            compileState.desc.independentAlphaBlendEnable =
               ctx->blend->pipe.independent_blend_enable;
            compileState.desc.alphaToCoverageEnable =
               ctx->blend->pipe.alpha_to_coverage;
            compileState.desc.sampleMaskEnable = 0; // XXX
            compileState.desc.numSamples = 1; // XXX

            compileState.alphaTestFunction =
               swr_convert_depth_func(ctx->depth_stencil->alpha.func);
            compileState.alphaTestFormat = ALPHA_TEST_FLOAT32; // xxx

            PFN_BLEND_JIT_FUNC func = NULL;
            auto search = ctx->blendJIT->find(compileState);
            if (search != ctx->blendJIT->end()) {
               func = search->second;
            } else {
               HANDLE hJitMgr = swr_screen(ctx->pipe.screen)->hJitMgr;
               func = JitCompileBlend(hJitMgr, compileState);
               debug_printf("BLEND shader %p\n", func);
               assert(func && "Error: BlendShader = NULL");

               ctx->blendJIT->insert(std::make_pair(compileState, func));
            }
            SwrSetBlendFunc(ctx->swrContext, target, func);
         }

      SwrSetBlendState(ctx->swrContext, &blendState);
   }

   if (ctx->dirty & SWR_NEW_STIPPLE) {
      /* XXX What to do with this one??? SWR doesn't stipple */
   }

   if (ctx->dirty & (SWR_NEW_VS | SWR_NEW_SO | SWR_NEW_RASTERIZER)) {
      ctx->vs->soState.rasterizerDisable =
         ctx->rasterizer->rasterizer_discard;
      SwrSetSoState(ctx->swrContext, &ctx->vs->soState);

      pipe_stream_output_info *stream_output = &ctx->vs->pipe.stream_output;

      for (uint32_t i = 0; i < ctx->num_so_targets; i++) {
         SWR_STREAMOUT_BUFFER buffer = {0};
         if (!ctx->so_targets[i])
            continue;
         buffer.enable = true;
         buffer.pBuffer =
            (uint32_t *)swr_resource_data(ctx->so_targets[i]->buffer);
         buffer.bufferSize = ctx->so_targets[i]->buffer_size >> 2;
         buffer.pitch = stream_output->stride[i];
         buffer.streamOffset = ctx->so_targets[i]->buffer_offset >> 2;

         SwrSetSoBuffers(ctx->swrContext, &buffer, i);
      }
   }

   uint32_t linkage = ctx->vs->linkageMask;
   if (ctx->rasterizer->sprite_coord_enable)
      linkage |= (1 << ctx->vs->info.base.num_outputs);

   SwrSetLinkage(ctx->swrContext, linkage, NULL);

   // set up frontend state
   SWR_FRONTEND_STATE feState = {0};
   SwrSetFrontendState(ctx->swrContext, &feState);

   // set up backend state
   SWR_BACKEND_STATE backendState = {0};
   backendState.numAttributes = 1;
   backendState.numComponents[0] = 4;
   backendState.constantInterpolationMask = ctx->fs->constantMask;
   backendState.pointSpriteTexCoordMask = ctx->fs->pointSpriteMask;

   SwrSetBackendState(ctx->swrContext, &backendState);

   ctx->dirty = post_update_dirty_flags;
}

static struct pipe_stream_output_target *
swr_create_so_target(struct pipe_context *pipe,
                     struct pipe_resource *buffer,
                     unsigned buffer_offset,
                     unsigned buffer_size)
{
   struct pipe_stream_output_target *target;

   target = CALLOC_STRUCT(pipe_stream_output_target);
   if (!target)
      return NULL;

   target->context = pipe;
   target->reference.count = 1;
   pipe_resource_reference(&target->buffer, buffer);
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;
   return target;
}

static void
swr_destroy_so_target(struct pipe_context *pipe,
                      struct pipe_stream_output_target *target)
{
   pipe_resource_reference(&target->buffer, NULL);
   FREE(target);
}

static void
swr_set_so_targets(struct pipe_context *pipe,
                   unsigned num_targets,
                   struct pipe_stream_output_target **targets,
                   const unsigned *offsets)
{
   struct swr_context *swr = swr_context(pipe);
   uint32_t i;

   assert(num_targets < MAX_SO_STREAMS);

   for (i = 0; i < num_targets; i++) {
      pipe_so_target_reference(
         (struct pipe_stream_output_target **)&swr->so_targets[i],
         targets[i]);
   }

   for (/* fall-through */; i < swr->num_so_targets; i++) {
      pipe_so_target_reference(
         (struct pipe_stream_output_target **)&swr->so_targets[i], NULL);
   }

   swr->num_so_targets = num_targets;

   swr->dirty = SWR_NEW_SO;
}


void
swr_state_init(struct pipe_context *pipe)
{
   pipe->create_blend_state = swr_create_blend_state;
   pipe->bind_blend_state = swr_bind_blend_state;
   pipe->delete_blend_state = swr_delete_blend_state;

   pipe->create_depth_stencil_alpha_state = swr_create_depth_stencil_state;
   pipe->bind_depth_stencil_alpha_state = swr_bind_depth_stencil_state;
   pipe->delete_depth_stencil_alpha_state = swr_delete_depth_stencil_state;

   pipe->create_rasterizer_state = swr_create_rasterizer_state;
   pipe->bind_rasterizer_state = swr_bind_rasterizer_state;
   pipe->delete_rasterizer_state = swr_delete_rasterizer_state;

   pipe->create_sampler_state = swr_create_sampler_state;
   pipe->bind_sampler_states = swr_bind_sampler_states;
   pipe->delete_sampler_state = swr_delete_sampler_state;

   pipe->create_sampler_view = swr_create_sampler_view;
   pipe->set_sampler_views = swr_set_sampler_views;
   pipe->sampler_view_destroy = swr_sampler_view_destroy;

   pipe->create_vs_state = swr_create_vs_state;
   pipe->bind_vs_state = swr_bind_vs_state;
   pipe->delete_vs_state = swr_delete_vs_state;

   pipe->create_fs_state = swr_create_fs_state;
   pipe->bind_fs_state = swr_bind_fs_state;
   pipe->delete_fs_state = swr_delete_fs_state;

   pipe->set_constant_buffer = swr_set_constant_buffer;

   pipe->create_vertex_elements_state = swr_create_vertex_elements_state;
   pipe->bind_vertex_elements_state = swr_bind_vertex_elements_state;
   pipe->delete_vertex_elements_state = swr_delete_vertex_elements_state;

   pipe->set_vertex_buffers = swr_set_vertex_buffers;
   pipe->set_index_buffer = swr_set_index_buffer;

   pipe->set_polygon_stipple = swr_set_polygon_stipple;
   pipe->set_clip_state = swr_set_clip_state;
   pipe->set_scissor_states = swr_set_scissor_states;
   pipe->set_viewport_states = swr_set_viewport_states;

   pipe->set_framebuffer_state = swr_set_framebuffer_state;

   pipe->set_blend_color = swr_set_blend_color;
   pipe->set_stencil_ref = swr_set_stencil_ref;

   pipe->set_sample_mask = swr_set_sample_mask;

   pipe->create_stream_output_target = swr_create_so_target;
   pipe->stream_output_target_destroy = swr_destroy_so_target;
   pipe->set_stream_output_targets = swr_set_so_targets;
}

/*!
 * \file painter_backend_gl.cpp
 * \brief file painter_backend_gl.cpp
 *
 * Copyright 2016 by Intel.
 *
 * Contact: kevin.rogovin@intel.com
 *
 * This Source Code Form is subject to the
 * terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with
 * this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 *
 * \author Kevin Rogovin <kevin.rogovin@intel.com>
 *
 */


#include <list>
#include <map>
#include <sstream>
#include <vector>
#include <iostream>

#include <fastuidraw/gl_backend/ngl_header.hpp>
#include <fastuidraw/gl_backend/gl_program.hpp>
#include <fastuidraw/gl_backend/opengl_trait.hpp>
#include <fastuidraw/gl_backend/gl_get.hpp>
#include <fastuidraw/gl_backend/gl_context_properties.hpp>

#include <private/util_private.hpp>
#include <private/util_private_ostream.hpp>
#include "painter_backend_gl.hpp"
#include "tex_buffer.hpp"
#include "texture_gl.hpp"
#include "painter_backend_gl_config.hpp"
#include "painter_vao_pool.hpp"
#include "painter_shader_registrar_gl.hpp"
#include "painter_surface_gl_private.hpp"
#include "binding_points.hpp"

#ifdef FASTUIDRAW_GL_USE_GLES
#define GL_SRC1_COLOR GL_SRC1_COLOR_EXT
#define GL_SRC1_ALPHA GL_SRC1_ALPHA_EXT
#define GL_ONE_MINUS_SRC1_COLOR GL_ONE_MINUS_SRC1_COLOR_EXT
#define GL_ONE_MINUS_SRC1_ALPHA GL_ONE_MINUS_SRC1_ALPHA_EXT
#define GL_CLIP_DISTANCE0 GL_CLIP_DISTANCE0_EXT
#endif

namespace
{
  class ImageBarrier:public fastuidraw::PainterDrawBreakAction
  {
  public:
    virtual
    fastuidraw::gpu_dirty_state
    execute(fastuidraw::PainterBackend*) const
    {
      fastuidraw_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      return fastuidraw::gpu_dirty_state();
    }
  };

  class ImageBarrierByRegion:public fastuidraw::PainterDrawBreakAction
  {
  public:
    virtual
    fastuidraw::gpu_dirty_state
    execute(fastuidraw::PainterBackend*) const
    {
      fastuidraw_glMemoryBarrierByRegion(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      return fastuidraw::gpu_dirty_state();
    }
  };

  class PainterBackendGLPrivate;

  class RenderTargetState
  {
  public:
    RenderTargetState(void):
      m_fbo(0),
      m_color_buffer_as_image(false)
    {}

    GLuint m_fbo;
    bool m_color_buffer_as_image;
  };

  class DrawState
  {
  public:
    explicit
    DrawState(void):
      m_current_program(nullptr),
      m_current_blend_mode(),
      m_blend_type(fastuidraw::PainterBlendShader::number_types)
    {}

    void
    on_pre_draw(PainterBackendGLPrivate *pr, GLuint current_fbo);

    void
    current_program(fastuidraw::gl::Program *p)
    {
      m_current_program = p;
    }

    fastuidraw::gl::Program*
    current_program(void) const
    {
      return m_current_program;
    }

    void
    current_blend_mode(const fastuidraw::BlendMode *p)
    {
      m_current_blend_mode = p;
    }

    void
    blend_type(enum fastuidraw::PainterBlendShader::shader_type v)
    {
      m_blend_type = v;
    }

    enum fastuidraw::PainterBlendShader::shader_type
    blend_type(void) const
    {
      return m_blend_type;
    }

    void
    restore_gl_state(const fastuidraw::gl::detail::painter_vao &vao,
                     PainterBackendGLPrivate *pr,
                     fastuidraw::gpu_dirty_state flags);

  private:
    static
    GLenum
    convert_blend_op(enum fastuidraw::BlendMode::equation_t v);

    static
    GLenum
    convert_blend_func(enum fastuidraw::BlendMode::func_t v);

    fastuidraw::gl::Program *m_current_program;
    const fastuidraw::BlendMode *m_current_blend_mode;
    enum fastuidraw::PainterBlendShader::shader_type m_blend_type;
    RenderTargetState m_current_render_target_state;
  };

  class PainterBackendGLPrivate
  {
  public:
    explicit
    PainterBackendGLPrivate(const fastuidraw::gl::PainterBackendFactoryGL *f,
                            fastuidraw::gl::PainterBackendGL *p);

    ~PainterBackendGLPrivate();

    GLuint
    clear_buffers_of_current_surface(bool clear_depth, bool clear_color);

    RenderTargetState
    set_gl_state(RenderTargetState last_state,
                 enum fastuidraw::PainterBlendShader::shader_type blend_type,
                 fastuidraw::gpu_dirty_state v);

    bool
    use_uber_shader(void)
    {
      return !m_cached_item_programs;
    }

    fastuidraw::reference_counted_ptr<fastuidraw::gl::detail::PainterShaderRegistrarGL> m_reg_gl;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::GlyphAtlasGL> m_glyph_atlas;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::ImageAtlasGL> m_image_atlas;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::ColorStopAtlasGL> m_colorstop_atlas;

    GLuint m_nearest_filter_sampler;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::detail::painter_vao_pool> m_pool;
    fastuidraw::gl::detail::PainterSurfaceGLPrivate *m_surface_gl;
    bool m_uniform_ubo_ready;
    std::vector<GLuint> m_current_external_texture;
    GLuint m_current_coverage_buffer_texture;
    fastuidraw::gl::detail::BindingPoints m_binding_points;
    DrawState m_draw_state;
    fastuidraw::gl::detail::PainterShaderRegistrarGL::program_set m_cached_programs;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::detail::PainterShaderRegistrarGL::CachedItemPrograms> m_cached_item_programs;
    fastuidraw::vecN<enum fastuidraw::gl::PainterBackendFactoryGL::program_type_t, 2> m_choose_uber_program;

    fastuidraw::gl::PainterBackendGL *m_p;
  };

  class TextureImageBindAction:public fastuidraw::PainterDrawBreakAction
  {
  public:
    typedef fastuidraw::gl::ImageAtlasGL::TextureImage TextureImage;

    TextureImageBindAction(unsigned int slot,
                           const fastuidraw::reference_counted_ptr<const fastuidraw::Image> &im,
                           PainterBackendGLPrivate *p):
      m_p(p),
      m_slot(slot),
      m_texture_unit(slot + p->m_binding_points.m_external_texture_binding)
    {
      FASTUIDRAWassert(im);
      FASTUIDRAWassert(im.dynamic_cast_ptr<const TextureImage>());
      m_image = im.static_cast_ptr<const TextureImage>();
    }

    virtual
    fastuidraw::gpu_dirty_state
    execute(fastuidraw::PainterBackend*) const
    {
      fastuidraw_glActiveTexture(GL_TEXTURE0 + m_texture_unit);
      fastuidraw_glBindTexture(GL_TEXTURE_2D, m_image->texture());

      /* if the user makes an action that affects texture
       * unit m_texture_unit, we need to give the backend
       * the knowledge of what is the external texture
       * so that it an correctly restore its state.
       */
      m_p->m_current_external_texture[m_slot] = m_image->texture();

      /* we do not regard changing the texture unit
       * as changing the GPU texture state because the
       * restore of GL state would be all those texture
       * states we did not change
       */
      return fastuidraw::gpu_dirty_state();
    }

  private:
    fastuidraw::reference_counted_ptr<const TextureImage> m_image;
    PainterBackendGLPrivate *m_p;
    unsigned int m_slot, m_texture_unit;
  };

  class CoverageTextureBindAction:public fastuidraw::PainterDrawBreakAction
  {
  public:
    typedef fastuidraw::gl::ImageAtlasGL::TextureImage TextureImage;

    CoverageTextureBindAction(const fastuidraw::reference_counted_ptr<const fastuidraw::Image> &im,
                              PainterBackendGLPrivate *p):
      m_p(p),
      m_texture_unit(p->m_binding_points.m_coverage_buffer_texture_binding)
    {
      FASTUIDRAWassert(im);
      FASTUIDRAWassert(im.dynamic_cast_ptr<const TextureImage>());
      m_image = im.static_cast_ptr<const TextureImage>();
    }

    virtual
    fastuidraw::gpu_dirty_state
    execute(fastuidraw::PainterBackend*) const
    {
      fastuidraw_glActiveTexture(GL_TEXTURE0 + m_texture_unit);
      fastuidraw_glBindTexture(GL_TEXTURE_2D, m_image->texture());

      /* if the user makes an action that affects texture
       * unit m_texture_unit, we need to give the backend
       * the knowledge of what is the texture so that it an
       * correctly restore its state.
       */
      m_p->m_current_coverage_buffer_texture = m_image->texture();

      /* we do not regard changing the texture unit
       * as changing the GPU texture state because the
       * restore of GL state would be all those texture
       * states we did not change
       */
      return fastuidraw::gpu_dirty_state();
    }

  private:
    fastuidraw::reference_counted_ptr<const TextureImage> m_image;
    PainterBackendGLPrivate *m_p;
    unsigned int m_texture_unit;
  };

  class DrawEntry
  {
  public:
    DrawEntry(const fastuidraw::BlendMode &mode,
              fastuidraw::gl::Program *new_program,
              enum fastuidraw::PainterBlendShader::shader_type blend_type);

    DrawEntry(const fastuidraw::BlendMode &mode);

    DrawEntry(const fastuidraw::reference_counted_ptr<const fastuidraw::PainterDrawBreakAction> &action);

    void
    add_entry(GLsizei count, const void *offset);

    void
    draw(PainterBackendGLPrivate *pr,
         const fastuidraw::gl::detail::painter_vao &vao,
         DrawState *st) const;

  private:
    bool m_set_blend;
    fastuidraw::BlendMode m_blend_mode;
    fastuidraw::reference_counted_ptr<const fastuidraw::PainterDrawBreakAction> m_action;

    std::vector<GLsizei> m_counts;
    std::vector<const GLvoid*> m_indices;
    fastuidraw::gl::Program *m_new_program;
    enum fastuidraw::PainterBlendShader::shader_type m_blend_type;
  };

  class DrawCommand:public fastuidraw::PainterDraw
  {
  public:
    explicit
    DrawCommand(const fastuidraw::reference_counted_ptr<fastuidraw::gl::detail::painter_vao_pool> &hnd,
                const fastuidraw::gl::PainterBackendFactoryGL::ConfigurationGL &params,
                PainterBackendGLPrivate *pr);

    virtual
    ~DrawCommand()
    {
      m_pool->release_vao(m_vao);
    }

    virtual
    bool
    draw_break(enum fastuidraw::PainterSurface::render_type_t render_type,
               const fastuidraw::PainterShaderGroup &old_shaders,
               const fastuidraw::PainterShaderGroup &new_shaders,
               unsigned int indices_written);

    virtual
    bool
    draw_break(const fastuidraw::reference_counted_ptr<const fastuidraw::PainterDrawBreakAction> &action,
               unsigned int indices_written);

    virtual
    void
    draw(void) const;

  protected:

    virtual
    void
    unmap_implement(unsigned int attributes_written,
                    unsigned int indices_written,
                    unsigned int data_store_written);

  private:

    void
    add_entry(unsigned int indices_written);

    PainterBackendGLPrivate *m_pr;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::detail::painter_vao_pool> m_pool;
    fastuidraw::gl::detail::painter_vao m_vao;
    unsigned int m_attributes_written, m_indices_written;
    std::list<DrawEntry> m_draws;
  };

  class SurfacePropertiesPrivate
  {
  public:
    SurfacePropertiesPrivate(void):
      m_dimensions(1, 1)
    {}

    fastuidraw::ivec2 m_dimensions;
  };
}

////////////////////////////////////////////
// DrawState methods
void
DrawState::
on_pre_draw(PainterBackendGLPrivate *pr, GLuint current_fbo)
{
  using namespace fastuidraw;
  using namespace fastuidraw::gl;

  /* We need to initialize what program and FBO are active */
  if (pr->m_surface_gl->m_render_type == PainterSurface::color_buffer_type)
    {
      enum PainterBackendFactoryGL::program_type_t pz;

      m_blend_type = pr->m_reg_gl->params().preferred_blend_type();
      pz = pr->m_choose_uber_program[false];
      m_current_program = pr->m_cached_programs.program(m_blend_type, pz).get();
    }
  else
    {
      m_current_program = pr->m_cached_programs.m_deferred_coverage_program.get();
      m_blend_type = PainterBlendShader::number_types;
    }

  RenderTargetState R;
  R.m_fbo = current_fbo;
  m_current_render_target_state = pr->set_gl_state(R, m_blend_type, gpu_dirty_state::all);
  m_current_program->use_program();
  m_current_blend_mode = nullptr;
}

void
DrawState::
restore_gl_state(const fastuidraw::gl::detail::painter_vao &vao,
                 PainterBackendGLPrivate *pr,
                 fastuidraw::gpu_dirty_state flags)
{
  using namespace fastuidraw;
  using namespace fastuidraw::gl;

  m_current_render_target_state = pr->set_gl_state(m_current_render_target_state, m_blend_type, flags);
  if (flags & gpu_dirty_state::shader)
    {
      FASTUIDRAWassert(m_current_program);
      m_current_program->use_program();
    }

  /* If necessary, restore the UBO or TBO assoicated to the data
   * store binding point.
   */
  switch(vao.m_data_store_backing)
    {
    case PainterBackendFactoryGL::data_store_tbo:
      if (flags & gpu_dirty_state::textures)
        {
          fastuidraw_glActiveTexture(GL_TEXTURE0 + vao.m_data_store_binding_point);
          fastuidraw_glBindTexture(GL_TEXTURE_BUFFER, vao.m_data_tbo);
        }
      break;

    case PainterBackendFactoryGL::data_store_ubo:
      if (flags & gpu_dirty_state::constant_buffers)
        {
          fastuidraw_glBindBufferBase(GL_UNIFORM_BUFFER, vao.m_data_store_binding_point, vao.m_data_bo);
        }
      break;

    case PainterBackendFactoryGL::data_store_ssbo:
      if (flags & gpu_dirty_state::storage_buffers)
        {
          fastuidraw_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, vao.m_data_store_binding_point, vao.m_data_bo);
        }
      break;

    default:
      FASTUIDRAWassert(!"Bad value for vao.m_data_store_backing");
    }

  if (flags & gpu_dirty_state::blend_mode)
    {
      FASTUIDRAWassert(m_current_blend_mode);
      FASTUIDRAWassert(m_current_blend_mode->is_valid());
      if (m_current_blend_mode->blending_on())
        {
          fastuidraw_glEnable(GL_BLEND);
          fastuidraw_glBlendEquationSeparate(convert_blend_op(m_current_blend_mode->equation_rgb()),
                                             convert_blend_op(m_current_blend_mode->equation_alpha()));
          fastuidraw_glBlendFuncSeparate(convert_blend_func(m_current_blend_mode->func_src_rgb()),
                                         convert_blend_func(m_current_blend_mode->func_dst_rgb()),
                                         convert_blend_func(m_current_blend_mode->func_src_alpha()),
                                         convert_blend_func(m_current_blend_mode->func_dst_alpha()));
        }
      else
        {
          fastuidraw_glDisable(GL_BLEND);
        }
    }
}

GLenum
DrawState::
convert_blend_op(enum fastuidraw::BlendMode::equation_t v)
{
#define C(X) case fastuidraw::BlendMode::X: return GL_FUNC_##X
#define D(X) case fastuidraw::BlendMode::X: return GL_##X

  switch(v)
    {
      C(ADD);
      C(SUBTRACT);
      C(REVERSE_SUBTRACT);
      D(MIN);
      D(MAX);

    case fastuidraw::BlendMode::NUMBER_OPS:
    default:
      FASTUIDRAWassert(!"Bad fastuidraw::BlendMode::op_t v");
    }
#undef C
#undef D

  FASTUIDRAWassert("Invalid blend_op_t");
  return GL_INVALID_ENUM;
}

GLenum
DrawState::
convert_blend_func(enum fastuidraw::BlendMode::func_t v)
{
#define C(X) case fastuidraw::BlendMode::X: return GL_##X
  switch(v)
    {
      C(ZERO);
      C(ONE);
      C(SRC_COLOR);
      C(ONE_MINUS_SRC_COLOR);
      C(SRC_ALPHA);
      C(ONE_MINUS_SRC_ALPHA);
      C(DST_COLOR);
      C(ONE_MINUS_DST_COLOR);
      C(DST_ALPHA);
      C(ONE_MINUS_DST_ALPHA);
      C(CONSTANT_COLOR);
      C(ONE_MINUS_CONSTANT_COLOR);
      C(CONSTANT_ALPHA);
      C(ONE_MINUS_CONSTANT_ALPHA);
      C(SRC_ALPHA_SATURATE);
      C(SRC1_COLOR);
      C(ONE_MINUS_SRC1_COLOR);
      C(SRC1_ALPHA);
      C(ONE_MINUS_SRC1_ALPHA);

    case fastuidraw::BlendMode::NUMBER_FUNCS:
    default:
      FASTUIDRAWassert(!"Bad fastuidraw::BlendMode::func_t v");
    }
#undef C
  FASTUIDRAWassert("Invalid blend_t");
  return GL_INVALID_ENUM;
}

///////////////////////////////////////////////
// DrawEntry methods
DrawEntry::
DrawEntry(const fastuidraw::BlendMode &mode,
          fastuidraw::gl::Program *new_program,
          enum fastuidraw::PainterBlendShader::shader_type blend_type):
  m_set_blend(true),
  m_blend_mode(mode),
  m_new_program(new_program),
  m_blend_type(blend_type)
{
}

DrawEntry::
DrawEntry(const fastuidraw::BlendMode &mode):
  m_set_blend(true),
  m_blend_mode(mode),
  m_new_program(nullptr),
  m_blend_type(fastuidraw::PainterBlendShader::number_types)
{
}

DrawEntry::
DrawEntry(const fastuidraw::reference_counted_ptr<const fastuidraw::PainterDrawBreakAction> &action):
  m_set_blend(false),
  m_action(action),
  m_new_program(nullptr),
  m_blend_type(fastuidraw::PainterBlendShader::number_types)
{
}

void
DrawEntry::
add_entry(GLsizei count, const void *offset)
{
  m_counts.push_back(count);
  m_indices.push_back(offset);
}

void
DrawEntry::
draw(PainterBackendGLPrivate *pr,
     const fastuidraw::gl::detail::painter_vao &vao,
     DrawState *st) const
{
  using namespace fastuidraw;
  using namespace fastuidraw::gl;

  uint32_t flags(0);

  if (m_action)
    {
      /* Rather than having something delicate to restore
       * the currently bound VAO, instead we unbind it
       * and rebind it after the action.
       */
      fastuidraw_glBindVertexArray(0);
      flags |= m_action->execute(pr->m_p);
      fastuidraw_glBindVertexArray(vao.m_vao);
    }

  if (m_set_blend)
    {
      st->current_blend_mode(&m_blend_mode);
      flags |= gpu_dirty_state::blend_mode;
    }

  if (m_new_program && st->current_program() != m_new_program)
    {
      st->current_program(m_new_program);
      flags |= gpu_dirty_state::shader;
    }

  if (m_blend_type != PainterBlendShader::number_types && st->blend_type() != m_blend_type)
    {
      st->blend_type(m_blend_type);
      flags |= gpu_dirty_state::blend_mode;
    }

  st->restore_gl_state(vao, pr, flags);

  if (m_counts.empty())
    {
      return;
    }

  FASTUIDRAWassert(m_counts.size() == m_indices.size());

  #ifndef FASTUIDRAW_GL_USE_GLES
    {
      fastuidraw_glMultiDrawElements(GL_TRIANGLES, &m_counts[0],
                                     opengl_trait<PainterIndex>::type,
                                     &m_indices[0], m_counts.size());
    }
  #else
    {
      if (pr->m_reg_gl->has_multi_draw_elements())
        {
          fastuidraw_glMultiDrawElementsEXT(GL_TRIANGLES, &m_counts[0],
                                            opengl_trait<PainterIndex>::type,
                                            &m_indices[0], m_counts.size());
        }
      else
        {
          for(unsigned int i = 0, endi = m_counts.size(); i < endi; ++i)
            {
              fastuidraw_glDrawElements(GL_TRIANGLES, m_counts[i],
                                        opengl_trait<PainterIndex>::type,
                                        m_indices[i]);
            }
        }
    }
  #endif
}

////////////////////////////////////
// DrawCommand methods
DrawCommand::
DrawCommand(const fastuidraw::reference_counted_ptr<fastuidraw::gl::detail::painter_vao_pool> &hnd,
            const fastuidraw::gl::PainterBackendFactoryGL::ConfigurationGL &params,
            PainterBackendGLPrivate *pr):
  m_pr(pr),
  m_pool(hnd),
  m_vao(m_pool->request_vao()),
  m_attributes_written(0),
  m_indices_written(0)
{
  /* map the buffers and set to the c_array<> fields of
   * fastuidraw::PainterDraw to the mapping location.
   */
  void *attr_bo, *index_bo, *data_bo, *header_bo;
  uint32_t flags;

  flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT;

  fastuidraw_glBindBuffer(GL_ARRAY_BUFFER, m_vao.m_attribute_bo);
  attr_bo = fastuidraw_glMapBufferRange(GL_ARRAY_BUFFER, 0, hnd->attribute_buffer_size(), flags);
  FASTUIDRAWassert(attr_bo != nullptr);

  fastuidraw_glBindBuffer(GL_ARRAY_BUFFER, m_vao.m_header_bo);
  header_bo = fastuidraw_glMapBufferRange(GL_ARRAY_BUFFER, 0, hnd->header_buffer_size(), flags);
  FASTUIDRAWassert(header_bo != nullptr);

  fastuidraw_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vao.m_index_bo);
  index_bo = fastuidraw_glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, hnd->index_buffer_size(), flags);
  FASTUIDRAWassert(index_bo != nullptr);

  fastuidraw_glBindBuffer(GL_ARRAY_BUFFER, m_vao.m_data_bo);
  data_bo = fastuidraw_glMapBufferRange(GL_ARRAY_BUFFER, 0, hnd->data_buffer_size(), flags);
  FASTUIDRAWassert(data_bo != nullptr);

  m_attributes = fastuidraw::c_array<fastuidraw::PainterAttribute>(static_cast<fastuidraw::PainterAttribute*>(attr_bo),
                                                                 params.attributes_per_buffer());
  m_indices = fastuidraw::c_array<fastuidraw::PainterIndex>(static_cast<fastuidraw::PainterIndex*>(index_bo),
                                                          params.indices_per_buffer());
  m_store = fastuidraw::c_array<fastuidraw::generic_data>(static_cast<fastuidraw::generic_data*>(data_bo),
                                                          hnd->data_buffer_size() / sizeof(fastuidraw::generic_data));

  m_header_attributes = fastuidraw::c_array<uint32_t>(static_cast<uint32_t*>(header_bo),
                                                     params.attributes_per_buffer());

  fastuidraw_glBindBuffer(GL_ARRAY_BUFFER, 0);
  fastuidraw_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

bool
DrawCommand::
draw_break(const fastuidraw::reference_counted_ptr<const fastuidraw::PainterDrawBreakAction> &action,
           unsigned int indices_written)
{
  bool return_value(false);

  FASTUIDRAWassert(action);
  if (!m_draws.empty())
    {
      return_value = true;
      add_entry(indices_written);
    }
  m_draws.push_back(action);
  return return_value;
}

bool
DrawCommand::
draw_break(enum fastuidraw::PainterSurface::render_type_t render_type,
           const fastuidraw::PainterShaderGroup &old_shaders,
           const fastuidraw::PainterShaderGroup &new_shaders,
           unsigned int indices_written)
{
  using namespace fastuidraw;
  using namespace fastuidraw::gl;
  using namespace fastuidraw::gl::detail;

  /* if the blend mode changes, then we need to start a new DrawEntry */
  BlendMode old_mode, new_mode;
  uint32_t new_disc, old_disc;
  bool return_value(false);
  enum PainterBlendShader::shader_type old_blend_type, new_blend_type;

  old_mode = old_shaders.blend_mode();
  new_mode = new_shaders.blend_mode();

  old_blend_type = old_shaders.blend_shader_type();
  new_blend_type = new_shaders.blend_shader_type();

  if (m_pr->use_uber_shader())
    {
      old_disc = old_shaders.item_group() & PainterShaderRegistrarGL::shader_group_discard_mask;
      new_disc = new_shaders.item_group() & PainterShaderRegistrarGL::shader_group_discard_mask;
    }
  else
    {
      old_disc = old_shaders.item_group();
      new_disc = new_shaders.item_group();
    }

  if (old_disc != new_disc || old_blend_type != new_blend_type)
    {
      Program *new_program;
      if (m_pr->use_uber_shader())
        {
          if (render_type == PainterSurface::color_buffer_type)
            {
              enum PainterBackendFactoryGL::program_type_t pz;
              pz = m_pr->m_choose_uber_program[new_disc != 0u];
              new_program = m_pr->m_cached_programs.program(pz, new_blend_type).get();
            }
          else
            {
              new_program = m_pr->m_cached_programs.m_deferred_coverage_program.get();
            }
        }
      else
        {
          new_program =
            m_pr->m_cached_item_programs->program_of_item_shader(render_type, new_disc, new_blend_type).get();
        }

      if (!m_draws.empty())
        {
          add_entry(indices_written);
          return_value = true;
        }

      FASTUIDRAWassert(new_program);
      m_draws.push_back(DrawEntry(fastuidraw::BlendMode(new_mode), new_program, new_blend_type));
      return return_value;
    }
  else if (old_mode != new_mode)
    {
      if (!m_draws.empty())
        {
          add_entry(indices_written);
          return_value = true;
        }
      m_draws.push_back(new_mode);
      return return_value;
    }
  else
    {
      /* any other state changes means that we just need to add an
       * entry to the current draw entry.
       */
      add_entry(indices_written);
      return false;
    }
}

void
DrawCommand::
draw(void) const
{
  using namespace fastuidraw;
  using namespace fastuidraw::gl;

  fastuidraw_glBindVertexArray(m_vao.m_vao);
  switch(m_vao.m_data_store_backing)
    {
    case PainterBackendFactoryGL::data_store_tbo:
      {
        fastuidraw_glActiveTexture(GL_TEXTURE0 + m_vao.m_data_store_binding_point);
        fastuidraw_glBindTexture(GL_TEXTURE_BUFFER, m_vao.m_data_tbo);
      }
      break;

    case PainterBackendFactoryGL::data_store_ubo:
      {
        fastuidraw_glBindBufferBase(GL_UNIFORM_BUFFER, m_vao.m_data_store_binding_point, m_vao.m_data_bo);
      }
      break;

    case PainterBackendFactoryGL::data_store_ssbo:
      {
        fastuidraw_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_vao.m_data_store_binding_point, m_vao.m_data_bo);
      }
      break;

    default:
      FASTUIDRAWassert(!"Bad value for m_vao.m_data_store_backing");
    }

  for(const DrawEntry &entry : m_draws)
    {
      entry.draw(m_pr, m_vao, &m_pr->m_draw_state);
    }
  fastuidraw_glBindVertexArray(0);
}

void
DrawCommand::
unmap_implement(unsigned int attributes_written,
                unsigned int indices_written,
                unsigned int data_store_written)
{
  m_attributes_written = attributes_written;
  add_entry(indices_written);
  FASTUIDRAWassert(m_indices_written == indices_written);

  fastuidraw_glBindBuffer(GL_ARRAY_BUFFER, m_vao.m_attribute_bo);
  fastuidraw_glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, attributes_written * sizeof(fastuidraw::PainterAttribute));
  fastuidraw_glUnmapBuffer(GL_ARRAY_BUFFER);

  fastuidraw_glBindBuffer(GL_ARRAY_BUFFER, m_vao.m_header_bo);
  fastuidraw_glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, attributes_written * sizeof(uint32_t));
  fastuidraw_glUnmapBuffer(GL_ARRAY_BUFFER);

  fastuidraw_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vao.m_index_bo);
  fastuidraw_glFlushMappedBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, indices_written * sizeof(fastuidraw::PainterIndex));
  fastuidraw_glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

  fastuidraw_glBindBuffer(GL_ARRAY_BUFFER, m_vao.m_data_bo);
  fastuidraw_glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, data_store_written * sizeof(fastuidraw::generic_data));
  fastuidraw_glUnmapBuffer(GL_ARRAY_BUFFER);
}

void
DrawCommand::
add_entry(unsigned int indices_written)
{
  unsigned int count;
  const fastuidraw::PainterIndex *offset(nullptr);

  if (m_draws.empty())
    {
      m_draws.push_back(fastuidraw::BlendMode());
    }
  FASTUIDRAWassert(indices_written >= m_indices_written);
  count = indices_written - m_indices_written;
  offset += m_indices_written;
  m_draws.back().add_entry(count, offset);
  m_indices_written = indices_written;
}

///////////////////////////////////
// PainterBackendGLPrivate methods
PainterBackendGLPrivate::
PainterBackendGLPrivate(const fastuidraw::gl::PainterBackendFactoryGL *f,
                        fastuidraw::gl::PainterBackendGL *p):
  m_nearest_filter_sampler(0),
  m_surface_gl(nullptr),
  m_p(p)
{
  using namespace fastuidraw;
  using namespace fastuidraw::gl;
  using namespace fastuidraw::gl::detail;

  reference_counted_ptr<PainterShaderRegistrar> reg_base(f->painter_shader_registrar());

  FASTUIDRAWassert(reg_base.dynamic_cast_ptr<PainterShaderRegistrarGL>());
  m_reg_gl = reg_base.static_cast_ptr<PainterShaderRegistrarGL>();

  FASTUIDRAWassert(f->glyph_atlas().dynamic_cast_ptr<GlyphAtlasGL>());
  m_glyph_atlas = f->glyph_atlas().static_cast_ptr<GlyphAtlasGL>();

  FASTUIDRAWassert(f->image_atlas().dynamic_cast_ptr<ImageAtlasGL>());
  m_image_atlas = f->image_atlas().static_cast_ptr<ImageAtlasGL>();

  FASTUIDRAWassert(f->colorstop_atlas().dynamic_cast_ptr<ColorStopAtlasGL>());
  m_colorstop_atlas = f->colorstop_atlas().static_cast_ptr<ColorStopAtlasGL>();

  m_binding_points.m_num_ubo_units = m_reg_gl->uber_shader_builder_params().num_ubo_units();
  m_binding_points.m_num_ssbo_units = m_reg_gl->uber_shader_builder_params().num_ssbo_units();
  m_binding_points.m_num_texture_units = m_reg_gl->uber_shader_builder_params().num_texture_units();
  m_binding_points.m_num_image_units = m_reg_gl->uber_shader_builder_params().num_image_units();

  m_binding_points.m_colorstop_atlas_binding = m_reg_gl->uber_shader_builder_params().colorstop_atlas_binding();
  m_binding_points.m_image_atlas_color_tiles_nearest_binding = m_reg_gl->uber_shader_builder_params().image_atlas_color_tiles_nearest_binding();
  m_binding_points.m_image_atlas_color_tiles_linear_binding = m_reg_gl->uber_shader_builder_params().image_atlas_color_tiles_linear_binding();
  m_binding_points.m_image_atlas_index_tiles_binding = m_reg_gl->uber_shader_builder_params().image_atlas_index_tiles_binding();
  m_binding_points.m_glyph_atlas_store_binding = m_reg_gl->uber_shader_builder_params().glyph_atlas_store_binding();
  m_binding_points.m_glyph_atlas_store_binding_fp16 = m_reg_gl->uber_shader_builder_params().glyph_atlas_store_binding_fp16x2();
  m_binding_points.m_data_store_buffer_binding = m_reg_gl->uber_shader_builder_params().data_store_buffer_binding();
  m_binding_points.m_color_interlock_image_buffer_binding = m_reg_gl->uber_shader_builder_params().color_interlock_image_buffer_binding();
  m_binding_points.m_external_texture_binding = m_reg_gl->uber_shader_builder_params().external_texture_binding();
  m_binding_points.m_coverage_buffer_texture_binding = m_reg_gl->uber_shader_builder_params().coverage_buffer_texture_binding();
  m_binding_points.m_uniforms_ubo_binding = m_reg_gl->uber_shader_builder_params().uniforms_ubo_binding();

  if (!m_reg_gl->params().use_uber_item_shader())
    {
      m_cached_item_programs = FASTUIDRAWnew PainterShaderRegistrarGL::CachedItemPrograms(m_reg_gl);
    }

  if (m_reg_gl->params().separate_program_for_discard())
    {
      m_choose_uber_program[false] = PainterBackendFactoryGL::program_without_discard;
      m_choose_uber_program[true] = PainterBackendFactoryGL::program_with_discard;
    }
  else
    {
      m_choose_uber_program[false] = m_choose_uber_program[true] = PainterBackendFactoryGL::program_all;
    }

  unsigned int num_ext(m_reg_gl->uber_shader_builder_params().number_external_textures());
  m_current_external_texture.resize(num_ext, 0u);
  m_pool = FASTUIDRAWnew painter_vao_pool(m_reg_gl->params(),
                                          m_reg_gl->tex_buffer_support(),
                                          m_binding_points.m_data_store_buffer_binding);
}

PainterBackendGLPrivate::
~PainterBackendGLPrivate()
{
  if (m_nearest_filter_sampler != 0)
    {
      fastuidraw_glDeleteSamplers(1, &m_nearest_filter_sampler);
    }
}

GLuint
PainterBackendGLPrivate::
clear_buffers_of_current_surface(bool clear_depth, bool clear_color_buffer)
{
  GLuint fbo(0);
  if (clear_depth || clear_color_buffer)
    {
      fastuidraw::c_array<const GLenum> draw_buffers;

      fbo = m_surface_gl->fbo(true);
      draw_buffers = m_surface_gl->draw_buffers(true);
      fastuidraw_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
      fastuidraw_glDrawBuffers(draw_buffers.size(), draw_buffers.c_ptr());

      if (clear_depth)
        {
          fastuidraw_glClearBufferfi(GL_DEPTH_STENCIL, 0, 0.0f, 0);
        }

      if (clear_color_buffer)
        {
          fastuidraw_glClearBufferfv(GL_COLOR, 0, m_surface_gl->m_clear_color.c_ptr());
        }
    }
  return fbo;
}

RenderTargetState
PainterBackendGLPrivate::
set_gl_state(RenderTargetState prev_state,
             enum fastuidraw::PainterBlendShader::shader_type blend_type,
             fastuidraw::gpu_dirty_state v)
{
  using namespace fastuidraw;
  using namespace fastuidraw::gl;
  using namespace fastuidraw::glsl;

  enum PainterBackendFactoryGL::fbf_blending_type_t fbf_blending_type;
  const PainterSurface::Viewport &vwp(m_surface_gl->m_viewport);
  ivec2 dimensions(m_surface_gl->m_dimensions);
  bool has_images;
  RenderTargetState return_value;

  if (m_surface_gl->m_render_type == PainterSurface::color_buffer_type)
    {
      fbf_blending_type = m_reg_gl->params().fbf_blending_type();

      FASTUIDRAWassert(blend_type != PainterBlendShader::number_types);
      return_value.m_color_buffer_as_image =
        (blend_type == PainterBlendShader::framebuffer_fetch
         && fbf_blending_type == PainterBackendFactoryGL::fbf_blending_interlock);

      has_images = return_value.m_color_buffer_as_image;

    }
  else
    {
      fbf_blending_type = PainterBackendFactoryGL::fbf_blending_not_supported;
      has_images = false;
      return_value.m_color_buffer_as_image = false;
    }

  if (m_surface_gl->m_render_type == PainterSurface::color_buffer_type
      && fbf_blending_type == PainterBackendFactoryGL::fbf_blending_interlock
      && return_value.m_color_buffer_as_image != prev_state.m_color_buffer_as_image)
    {
      if (return_value.m_color_buffer_as_image)
        {
          /* rendering is changing from using framebuffer to
           * using image-load-store.
           */
          fastuidraw_glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);

          /* make sure that the color-buffer gets bound as an image */
          v |= gpu_dirty_state::images;
        }
      else
        {
          /* rendering is changing from using image-load-store
           * to using framebuffer.
           */
          fastuidraw_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }
    }

  return_value.m_fbo = m_surface_gl->fbo(!return_value.m_color_buffer_as_image);
  if (return_value.m_fbo != prev_state.m_fbo || (v & gpu_dirty_state::render_target) != 0)
    {
      c_array<const GLenum> draw_buffers;

      draw_buffers = m_surface_gl->draw_buffers(!return_value.m_color_buffer_as_image);
      fastuidraw_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, return_value.m_fbo);
      fastuidraw_glDrawBuffers(draw_buffers.size(), draw_buffers.c_ptr());
      v |= gpu_dirty_state::viewport_scissor;
    }

  if (fbf_blending_type == PainterBackendFactoryGL::fbf_blending_interlock
      && ((v & gpu_dirty_state::images) != 0
          || return_value.m_color_buffer_as_image != prev_state.m_color_buffer_as_image))
    {
      if (return_value.m_color_buffer_as_image)
        {
          fastuidraw_glBindImageTexture(m_binding_points.m_color_interlock_image_buffer_binding,
                                        m_surface_gl->color_buffer(), //texture
                                        0, //level
                                        GL_FALSE, //layered
                                        0, //layer
                                        GL_READ_WRITE, //access
                                        GL_RGBA8);
        }
      else
        {
          fastuidraw_glBindImageTexture(m_binding_points.m_color_interlock_image_buffer_binding,
                                        0, //texture
                                        0, //level
                                        GL_FALSE, //layered
                                        0, //layer
                                        GL_READ_WRITE, //access
                                        GL_RGBA8);
        }
    }

  if (v & gpu_dirty_state::depth_stencil)
    {
      fastuidraw_glEnable(GL_DEPTH_TEST);
      fastuidraw_glDepthFunc(GL_GEQUAL);
      fastuidraw_glDisable(GL_STENCIL_TEST);
    }

  if (v & gpu_dirty_state::buffer_masks)
    {
      fastuidraw_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      fastuidraw_glDepthMask(GL_TRUE);
    }

  if (v & gpu_dirty_state::viewport_scissor)
    {
      if (dimensions.x() > vwp.m_dimensions.x()
          || dimensions.y() > vwp.m_dimensions.y()
          || vwp.m_origin.x() != 0
          || vwp.m_origin.y() != 0)
        {
          fastuidraw_glEnable(GL_SCISSOR_TEST);
          fastuidraw_glScissor(vwp.m_origin.x(), vwp.m_origin.y(),
                               vwp.m_dimensions.x(), vwp.m_dimensions.y());
        }
      else
        {
          fastuidraw_glDisable(GL_SCISSOR_TEST);
        }

      fastuidraw_glViewport(vwp.m_origin.x(), vwp.m_origin.y(),
                            vwp.m_dimensions.x(), vwp.m_dimensions.y());
    }

  if ((v & gpu_dirty_state::hw_clip) && m_reg_gl->number_clip_planes() > 0)
    {
      if (m_reg_gl->params().clipping_type() == PainterBackendFactoryGL::clipping_via_gl_clip_distance)
        {
          for (int i = 0; i < 4; ++i)
            {
              fastuidraw_glEnable(GL_CLIP_DISTANCE0 + i);
            }
        }
      else
        {
          for (int i = 0; i < 4; ++i)
            {
              fastuidraw_glDisable(GL_CLIP_DISTANCE0 + i);
            }
        }

      for(unsigned int i = 4; i < m_reg_gl->number_clip_planes(); ++i)
        {
          fastuidraw_glDisable(GL_CLIP_DISTANCE0 + i);
        }
    }

  if (v & gpu_dirty_state::textures)
    {
      fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_image_atlas_color_tiles_nearest_binding);
      fastuidraw_glBindSampler(m_binding_points.m_image_atlas_color_tiles_nearest_binding, m_nearest_filter_sampler);
      fastuidraw_glBindTexture(GL_TEXTURE_2D_ARRAY, m_image_atlas->color_texture());

      fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_image_atlas_color_tiles_linear_binding);
      fastuidraw_glBindSampler(m_binding_points.m_image_atlas_color_tiles_linear_binding, 0);
      fastuidraw_glBindTexture(GL_TEXTURE_2D_ARRAY, m_image_atlas->color_texture());

      fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_image_atlas_index_tiles_binding);
      fastuidraw_glBindSampler(m_binding_points.m_image_atlas_index_tiles_binding, 0);
      fastuidraw_glBindTexture(GL_TEXTURE_2D_ARRAY, m_image_atlas->index_texture());

      if (m_glyph_atlas->data_binding_point_is_texture_unit())
        {
          fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_glyph_atlas_store_binding);
          fastuidraw_glBindSampler(m_binding_points.m_glyph_atlas_store_binding, 0);
          fastuidraw_glBindTexture(m_glyph_atlas->data_binding_point(), m_glyph_atlas->data_backing(GlyphAtlasGL::backing_uint32_fmt));

          fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_glyph_atlas_store_binding_fp16);
          fastuidraw_glBindSampler(m_binding_points.m_glyph_atlas_store_binding_fp16, 0);
          fastuidraw_glBindTexture(m_glyph_atlas->data_binding_point(), m_glyph_atlas->data_backing(GlyphAtlasGL::backing_fp16x2_fmt));
        }

      fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_colorstop_atlas_binding);
      fastuidraw_glBindSampler(m_binding_points.m_colorstop_atlas_binding, 0);
      fastuidraw_glBindTexture(ColorStopAtlasGL::texture_bind_target(), m_colorstop_atlas->texture());

      for (unsigned int i = 0, endi = m_current_external_texture.size(); i < endi; ++i)
        {
          fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_external_texture_binding + i);
          fastuidraw_glBindTexture(GL_TEXTURE_2D, m_current_external_texture[i]);
          fastuidraw_glBindSampler(m_binding_points.m_external_texture_binding + i, 0);
        }

      fastuidraw_glActiveTexture(GL_TEXTURE0 + m_binding_points.m_coverage_buffer_texture_binding);
      fastuidraw_glBindTexture(GL_TEXTURE_2D, m_current_coverage_buffer_texture);
      fastuidraw_glBindSampler(m_binding_points.m_coverage_buffer_texture_binding, 0);

      /* QUESTION: should we restore the bindings of all of the externals? */
    }

  if (v & gpu_dirty_state::constant_buffers)
    {
      GLuint ubo;
      unsigned int size_generics(PainterShaderRegistrarGLSL::ubo_size());
      unsigned int size_bytes(sizeof(generic_data) * size_generics);
      void *ubo_mapped;

      /* Grabs and binds the buffer */
      ubo = m_pool->uniform_ubo(size_bytes, GL_UNIFORM_BUFFER);
      FASTUIDRAWassert(ubo != 0);

      if (!m_uniform_ubo_ready)
        {
          c_array<generic_data> ubo_mapped_ptr;
          ubo_mapped = fastuidraw_glMapBufferRange(GL_UNIFORM_BUFFER, 0, size_bytes,
                                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT
                                                   | GL_MAP_FLUSH_EXPLICIT_BIT);
          ubo_mapped_ptr = c_array<generic_data>(static_cast<generic_data*>(ubo_mapped),
                                                 size_generics);

          m_reg_gl->fill_uniform_buffer(m_surface_gl->m_viewport, ubo_mapped_ptr);
          fastuidraw_glFlushMappedBufferRange(GL_UNIFORM_BUFFER, 0, size_bytes);
          fastuidraw_glUnmapBuffer(GL_UNIFORM_BUFFER);
          m_uniform_ubo_ready = true;
        }

      fastuidraw_glBindBufferBase(GL_UNIFORM_BUFFER, m_binding_points.m_uniforms_ubo_binding, ubo);
    }

  if (v & gpu_dirty_state::storage_buffers)
    {
      if (!m_glyph_atlas->data_binding_point_is_texture_unit())
        {
          fastuidraw_glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                                      m_binding_points.m_glyph_atlas_store_binding,
                                      m_glyph_atlas->data_backing(GlyphAtlasGL::backing_uint32_fmt));
        }
    }

  return return_value;
}


///////////////////////////////////////////////
// fastuidraw::gl::PainterBackendGL methods
fastuidraw::gl::PainterBackendGL::
PainterBackendGL(const fastuidraw::gl::PainterBackendFactoryGL *f):
  PainterBackend()
{
  PainterBackendGLPrivate *d;
  m_d = d = FASTUIDRAWnew PainterBackendGLPrivate(f, this);
}

fastuidraw::gl::PainterBackendGL::
~PainterBackendGL()
{
  PainterBackendGLPrivate *d;
  d = static_cast<PainterBackendGLPrivate*>(m_d);
  FASTUIDRAWdelete(d);
  m_d = nullptr;
}

unsigned int
fastuidraw::gl::PainterBackendGL::
attribs_per_mapping(void) const
{
  PainterBackendGLPrivate *d;
  d = static_cast<PainterBackendGLPrivate*>(m_d);
  return d->m_reg_gl->params().attributes_per_buffer();
}

unsigned int
fastuidraw::gl::PainterBackendGL::
indices_per_mapping(void) const
{
  PainterBackendGLPrivate *d;
  d = static_cast<PainterBackendGLPrivate*>(m_d);
  return d->m_reg_gl->params().indices_per_buffer();
}

void
fastuidraw::gl::PainterBackendGL::
on_pre_draw(const reference_counted_ptr<PainterSurface> &surface,
            bool clear_color_buffer,
            bool begin_new_target)
{
  PainterBackendGLPrivate *d;

  d = static_cast<PainterBackendGLPrivate*>(m_d);
  d->m_surface_gl = static_cast<detail::PainterSurfaceGLPrivate*>(detail::PainterSurfaceGLPrivate::surface_gl(surface)->m_d);

  if (d->m_nearest_filter_sampler == 0)
    {
      fastuidraw_glGenSamplers(1, &d->m_nearest_filter_sampler);
      FASTUIDRAWassert(d->m_nearest_filter_sampler != 0);
      fastuidraw_glSamplerParameteri(d->m_nearest_filter_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      fastuidraw_glSamplerParameteri(d->m_nearest_filter_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    }

  GLuint fbo;

  d->m_uniform_ubo_ready = false;
  std::fill(d->m_current_external_texture.begin(), d->m_current_external_texture.end(), 0);
  d->m_current_coverage_buffer_texture = 0;
  fbo = d->clear_buffers_of_current_surface(begin_new_target, clear_color_buffer);
  d->m_draw_state.on_pre_draw(d, fbo);
}

void
fastuidraw::gl::PainterBackendGL::
on_post_draw(void)
{
  PainterBackendGLPrivate *d;
  d = static_cast<PainterBackendGLPrivate*>(m_d);

  /* this is somewhat paranoid to make sure that
   * the GL objects do not leak...
   */
  fastuidraw_glUseProgram(0);
  fastuidraw_glBindVertexArray(0);

  const PainterBackendFactoryGL::ConfigurationGL &params(d->m_reg_gl->params());

  fastuidraw_glActiveTexture(GL_TEXTURE0 + d->m_binding_points.m_image_atlas_color_tiles_nearest_binding);
  fastuidraw_glBindSampler(d->m_binding_points.m_image_atlas_color_tiles_nearest_binding, 0);
  fastuidraw_glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  fastuidraw_glActiveTexture(GL_TEXTURE0 + d->m_binding_points.m_image_atlas_color_tiles_linear_binding);
  fastuidraw_glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  fastuidraw_glActiveTexture(GL_TEXTURE0 + d->m_binding_points.m_image_atlas_index_tiles_binding);
  fastuidraw_glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  if (d->m_glyph_atlas->data_binding_point_is_texture_unit())
    {
      fastuidraw_glActiveTexture(GL_TEXTURE0 + d->m_binding_points.m_glyph_atlas_store_binding);
      fastuidraw_glBindTexture(d->m_glyph_atlas->data_binding_point(), 0);
    }
  else
    {
      fastuidraw_glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                                  d->m_binding_points.m_glyph_atlas_store_binding,
                                  0);
    }

  fastuidraw_glActiveTexture(GL_TEXTURE0 + d->m_binding_points.m_colorstop_atlas_binding);
  fastuidraw_glBindTexture(ColorStopAtlasGL::texture_bind_target(), 0);

  if (params.fbf_blending_type() == fbf_blending_interlock)
    {
      fastuidraw_glBindImageTexture(d->m_binding_points.m_color_interlock_image_buffer_binding, 0,
                                    0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    }

  switch(params.data_store_backing())
    {
    case data_store_tbo:
      {
        fastuidraw_glActiveTexture(GL_TEXTURE0 + d->m_binding_points.m_data_store_buffer_binding);
        fastuidraw_glBindTexture(GL_TEXTURE_BUFFER, 0);
      }
      break;

    case data_store_ubo:
      {
        fastuidraw_glBindBufferBase(GL_UNIFORM_BUFFER, d->m_binding_points.m_data_store_buffer_binding, 0);
      }
      break;

    case data_store_ssbo:
      {
        fastuidraw_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, d->m_binding_points.m_data_store_buffer_binding, 0);
      }
      break;

    default:
      FASTUIDRAWassert(!"Bad value for params.data_store_backing()");
    }
  fastuidraw_glBindBufferBase(GL_UNIFORM_BUFFER, d->m_binding_points.m_uniforms_ubo_binding, 0);
  fastuidraw_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  fastuidraw_glDisable(GL_SCISSOR_TEST);
  d->m_pool->next_pool();
}

fastuidraw::reference_counted_ptr<fastuidraw::PainterDrawBreakAction>
fastuidraw::gl::PainterBackendGL::
bind_image(unsigned int slot,
           const reference_counted_ptr<const Image> &im)
{
  PainterBackendGLPrivate *d;

  /* TODO: instead of creating an action each time
   * bind_image(), create the action once, attach it
   * to the image and retrieve the action instead.
   */
  d = static_cast<PainterBackendGLPrivate*>(m_d);
  return FASTUIDRAWnew TextureImageBindAction(slot, im, d);
}

fastuidraw::reference_counted_ptr<fastuidraw::PainterDrawBreakAction>
fastuidraw::gl::PainterBackendGL::
bind_coverage_surface(const reference_counted_ptr<PainterSurface> &surface)
{
  PainterBackendGLPrivate *d;

  /* TODO: instead of creating an action each time
   * bind_coverage_surface(), create the action once,
   * attach it to the image and retrieve the action
   * instead.
   */
  d = static_cast<PainterBackendGLPrivate*>(m_d);
  return FASTUIDRAWnew CoverageTextureBindAction(surface->image(d->m_image_atlas), d);
}

fastuidraw::reference_counted_ptr<fastuidraw::PainterDraw>
fastuidraw::gl::PainterBackendGL::
map_draw(void)
{
  PainterBackendGLPrivate *d;
  d = static_cast<PainterBackendGLPrivate*>(m_d);

  return FASTUIDRAWnew DrawCommand(d->m_pool, d->m_reg_gl->params(), d);
}

unsigned int
fastuidraw::gl::PainterBackendGL::
on_painter_begin(void)
{
  PainterBackendGLPrivate *d;
  d = static_cast<PainterBackendGLPrivate*>(m_d);
  d->m_cached_programs = d->m_reg_gl->programs();
  if (d->m_cached_item_programs)
    {
      d->m_cached_item_programs->reset();
    }
  return d->m_current_external_texture.size();
}
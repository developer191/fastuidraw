/*!
 * \file painter_custom_brush_shader.hpp
 * \brief file painter_custom_brush_shader.hpp
 *
 * Copyright 2019 by Intel.
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


#pragma once
#include <fastuidraw/painter/shader/painter_shader.hpp>

namespace fastuidraw
{

/*!\addtogroup PainterShaders
 * @{
 */

  /*!
   * \brief
   * A PainterCustomBrushShader represents a shader
   * for performing a custom brush coloring.
   */
  class PainterCustomBrushShader:public PainterShader
  {
  public:
    /*!
     * Ctor for creating a PainterCustomBrushShader which has multiple
     * sub-shaders. The purpose of sub-shaders is for the case
     * where multiple shaders have almost same code and those
     * code differences can be realized by examining a sub-shader
     * ID.
     * \param num_sub_shaders number of sub-shaders
     */
    explicit
    PainterCustomBrushShader(unsigned int num_sub_shaders = 1):
      PainterShader(num_sub_shaders)
    {}

    /*!
     * Ctor to create a PainterCustomBrushShader realized as a sub-shader
     * of an existing PainterCustomBrushShader
     * \param parent parent PainterCustomBrushShader that has sub-shaders
     * \param sub_shader which sub-shader of the parent PainterCustomBrushShader
     */
    PainterCustomBrushShader(reference_counted_ptr<PainterCustomBrushShader> parent,
                             unsigned int sub_shader):
      PainterShader(parent, sub_shader)
    {}
  };

/*! @} */
}

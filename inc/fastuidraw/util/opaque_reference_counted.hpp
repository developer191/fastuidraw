/*!
 * \file opaque_reference_counted.hpp
 * \brief file opaque_reference_counted.hpp
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
#pragma once

#include <fastuidraw/util/reference_counted.hpp>

namespace fastuidraw
{
/*!\addtogroup Utility
 * @{
 */

  /*!
   * \brief
   * Inherit from a member class of \ref opaque_reference_counted in
   * order to implement the PIMPL pattern for reference counted
   * data and the reference to be the type
   * reference_counted_ptr<opaque_reference_counted::base>.
   */
  class opaque_reference_counted
  {
  public:
    /*!
     * \brief
     * Empty base class
     */
    class base {};

    /*!
     * \brief
     * Typedef to reference counting which is NOT thread safe
     */
    typename reference_counted<base>::non_concurrent non_concurrent;

    /*!
     * \brief
     * Typedef to reference counting which is thread safe by locking
     * a mutex on adding and removing reference
     */
    typename reference_counted<base>::mutex mutex;

    /*!
     * \brief
     * Typedef to reference counting which is thread safe by atomically
     * adding and removing reference
     */
    typename reference_counted<base>::atomic atomic;

    /*!
     * \brief
     * Typedef for "default" way to reference count.
     */
    typename reference_counted<base>::concurrent concurrent;
  };

/*! @} */

} //namespace fastuidraw

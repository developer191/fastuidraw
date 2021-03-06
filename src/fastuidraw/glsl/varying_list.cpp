/*!
 * \file varying_list.cpp
 * \brief file varying_list.cpp
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


#include <fastuidraw/util/fastuidraw_memory.hpp>
#include <fastuidraw/glsl/varying_list.hpp>
#include <private/util_private.hpp>

namespace
{
  class VaryingListPrivate
  {
  public:
    enum
      {
        interpolation_number_types = fastuidraw::glsl::varying_list::interpolation_number_types
      };

    fastuidraw::vecN<fastuidraw::string_array, interpolation_number_types> m_floats;
    fastuidraw::string_array m_ints;
    fastuidraw::string_array m_uints;
    fastuidraw::string_array m_alias_list_names, m_alias_list_alias_names;
  };
}

/////////////////////////////////////////////
// fastuidraw::glsl::varying_list methods
fastuidraw::glsl::varying_list::
varying_list(void)
{
  m_d = FASTUIDRAWnew VaryingListPrivate();
}

fastuidraw::glsl::varying_list::
varying_list(const varying_list &rhs)
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(rhs.m_d);
  m_d = FASTUIDRAWnew VaryingListPrivate(*d);
}

fastuidraw::glsl::varying_list::
~varying_list()
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  FASTUIDRAWdelete(d);
  m_d = nullptr;
}

assign_swap_implement(fastuidraw::glsl::varying_list)

fastuidraw::c_array<const fastuidraw::c_string>
fastuidraw::glsl::varying_list::
floats(enum interpolation_qualifier_t q) const
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  return d->m_floats[q].get();
}

fastuidraw::c_array<const fastuidraw::c_string>
fastuidraw::glsl::varying_list::
uints(void) const
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  return d->m_uints.get();
}

fastuidraw::c_array<const fastuidraw::c_string>
fastuidraw::glsl::varying_list::
ints(void) const
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  return d->m_ints.get();
}

fastuidraw::glsl::varying_list&
fastuidraw::glsl::varying_list::
add_float(c_string pname, enum interpolation_qualifier_t q)
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  d->m_floats[q].push_back(pname);
  return *this;
}

fastuidraw::glsl::varying_list&
fastuidraw::glsl::varying_list::
add_uint(c_string pname)
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  d->m_uints.push_back(pname);
  return *this;
}

fastuidraw::glsl::varying_list&
fastuidraw::glsl::varying_list::
add_int(c_string pname)
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  d->m_ints.push_back(pname);
  return *this;
}

fastuidraw::glsl::varying_list&
fastuidraw::glsl::varying_list::
add_alias(c_string name, c_string alias_name)
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  d->m_alias_list_names.push_back(name);
  d->m_alias_list_alias_names.push_back(alias_name);
  return *this;
}

fastuidraw::c_array<const fastuidraw::c_string>
fastuidraw::glsl::varying_list::
alias_list_names(void) const
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  return d->m_alias_list_names.get();
}

fastuidraw::c_array<const fastuidraw::c_string>
fastuidraw::glsl::varying_list::
alias_list_alias_names(void) const
{
  VaryingListPrivate *d;
  d = static_cast<VaryingListPrivate*>(m_d);
  return d->m_alias_list_alias_names.get();
}

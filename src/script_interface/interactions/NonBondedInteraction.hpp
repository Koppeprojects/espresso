/*
 * Copyright (C) 2022 The ESPResSo project
 *
 * This file is part of ESPResSo.
 *
 * ESPResSo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ESPResSo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 *  The ScriptInterface counterparts of the non-bonded interactions parameters
 *  structs from the core are defined here.
 */

#ifndef SCRIPT_INTERFACE_INTERACTIONS_NONBONDED_INTERACTION_HPP
#define SCRIPT_INTERFACE_INTERACTIONS_NONBONDED_INTERACTION_HPP

#include "script_interface/ScriptInterface.hpp"
#include "script_interface/auto_parameters/AutoParameters.hpp"
#include "script_interface/get_value.hpp"

#include "core/event.hpp"
#include "core/nonbonded_interactions/nonbonded_interaction_data.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ScriptInterface {
namespace Interactions {

template <class CoreIA>
class InteractionPotentialInterface
    : public AutoParameters<InteractionPotentialInterface<CoreIA>> {
  std::array<int, 2> m_types = {-1, -1};

public:
  using CoreInteraction = CoreIA;

protected:
  using AutoParameters<InteractionPotentialInterface<CoreIA>>::context;
  std::shared_ptr<CoreInteraction> m_ia_si;
  virtual CoreInteraction IA_parameters::*get_ptr_offset() const = 0;
  virtual void make_new_instance(VariantMap const &params) = 0;

  template <typename T>
  auto make_autoparameter(T CoreInteraction::*ptr, char const *name) {
    return AutoParameter{name, AutoParameter::read_only,
                         [this, ptr]() { return m_ia_si.get()->*ptr; }};
  }

public:
  Variant do_call_method(std::string const &name,
                         VariantMap const &params) override {
    if (name == "set_params") {
      context()->parallel_try_catch(
          [this, &params]() { make_new_instance(params); });
      if (m_types[0] != -1) {
        copy_si_to_core();
        on_non_bonded_ia_change();
      }
      return {};
    }
    if (name == "bind_types") {
      auto types = get_value<std::vector<int>>(params, "_types");
      if (types[0] > types[1]) {
        std::swap(types[0], types[1]);
      }
      if (m_types[0] == -1 or
          (m_types[0] == types[0] and m_types[1] == types[1])) {
        m_types[0] = types[0];
        m_types[1] = types[1];
      } else {
        context()->parallel_try_catch([this]() {
          throw std::runtime_error(
              "Non-bonded interaction is already bound to interaction pair [" +
              std::to_string(m_types[0]) + ", " + std::to_string(m_types[1]) +
              "]");
        });
      }
      return {};
    }
    return {};
  }

  void do_construct(VariantMap const &params) final {
    if (params.empty()) {
      m_ia_si = std::make_shared<CoreInteraction>();
    } else if (params.count("_types") != 0) {
      do_call_method("bind_types", params);
      m_ia_si = std::make_shared<CoreInteraction>();
      copy_core_to_si();
    } else {
      context()->parallel_try_catch(
          [this, &params]() { make_new_instance(params); });
    }
  }

  auto const &get_ia() const { return *m_ia_si; }

  void copy_si_to_core() {
    assert(m_ia_si != nullptr);
    auto const key = get_ia_param_key(m_types[0], m_types[1]);
    assert(key < ::nonbonded_ia_params.size());
    ::nonbonded_ia_params[key].get()->*get_ptr_offset() = *m_ia_si;
    ::old_nonbonded_ia_params[key].*get_ptr_offset() = *m_ia_si;
  }

  void copy_core_to_si() {
    assert(m_ia_si != nullptr);
    auto const key = get_ia_param_key(m_types[0], m_types[1]);
    assert(key < ::nonbonded_ia_params.size());
    *m_ia_si = ::nonbonded_ia_params[key].get()->*get_ptr_offset();
  }
};

#ifdef WCA
class InteractionWCA : public InteractionPotentialInterface<::WCA_Parameters> {
protected:
  CoreInteraction IA_parameters::*get_ptr_offset() const override {
    return &::IA_parameters::wca;
  }

public:
  InteractionWCA() {
    add_parameters({
        make_autoparameter(&CoreInteraction::eps, "epsilon"),
        make_autoparameter(&CoreInteraction::sig, "sigma"),
    });
  }

  void make_new_instance(VariantMap const &params) override {
    m_ia_si = make_shared_from_args<CoreInteraction, double, double>(
        params, "epsilon", "sigma");
  }

  Variant do_call_method(std::string const &name,
                         VariantMap const &params) override {
    if (name == "get_cutoff") {
      return m_ia_si.get()->cut;
    }
    return InteractionPotentialInterface<CoreInteraction>::do_call_method(
        name, params);
  }
};
#endif // WCA

#ifdef LENNARD_JONES
class InteractionLJ : public InteractionPotentialInterface<::LJ_Parameters> {
protected:
  CoreInteraction IA_parameters::*get_ptr_offset() const override {
    return &::IA_parameters::lj;
  }

public:
  InteractionLJ() {
    add_parameters({
        make_autoparameter(&CoreInteraction::eps, "epsilon"),
        make_autoparameter(&CoreInteraction::sig, "sigma"),
        make_autoparameter(&CoreInteraction::cut, "cutoff"),
        make_autoparameter(&CoreInteraction::shift, "shift"),
        make_autoparameter(&CoreInteraction::offset, "offset"),
        make_autoparameter(&CoreInteraction::min, "min"),
    });
  }

  void make_new_instance(VariantMap const &params) override {
    if (auto const *shift = boost::get<std::string>(&params.at("shift"))) {
      if (*shift != "auto") {
        throw std::invalid_argument(
            "LJ parameter 'shift' has to be 'auto' or a float");
      }
      m_ia_si = make_shared_from_args<CoreInteraction, double, double, double,
                                      double, double>(
          params, "epsilon", "sigma", "cutoff", "offset", "min");
    } else {
      m_ia_si = make_shared_from_args<CoreInteraction, double, double, double,
                                      double, double, double>(
          params, "epsilon", "sigma", "cutoff", "offset", "min", "shift");
    }
  }
};
#endif // LENNARD_JONES

#ifdef LENNARD_JONES_GENERIC
class InteractionLJGen
    : public InteractionPotentialInterface<::LJGen_Parameters> {
protected:
  CoreInteraction IA_parameters::*get_ptr_offset() const override {
    return &::IA_parameters::ljgen;
  }

public:
  InteractionLJGen() {
    add_parameters({
        make_autoparameter(&CoreInteraction::eps, "epsilon"),
        make_autoparameter(&CoreInteraction::sig, "sigma"),
        make_autoparameter(&CoreInteraction::cut, "cutoff"),
        make_autoparameter(&CoreInteraction::shift, "shift"),
        make_autoparameter(&CoreInteraction::offset, "offset"),
#ifdef LJGEN_SOFTCORE
        make_autoparameter(&CoreInteraction::lambda, "lam"),
        make_autoparameter(&CoreInteraction::softrad, "delta"),
#endif
        make_autoparameter(&CoreInteraction::a1, "e1"),
        make_autoparameter(&CoreInteraction::a2, "e2"),
        make_autoparameter(&CoreInteraction::b1, "b1"),
        make_autoparameter(&CoreInteraction::b2, "b2"),
    });
  }

  void make_new_instance(VariantMap const &params) override {
    auto new_params = params;
    auto const *shift_string = boost::get<std::string>(&params.at("shift"));
    if (shift_string != nullptr) {
      if (*shift_string != "auto") {
        throw std::invalid_argument(
            "Generic LJ parameter 'shift' has to be 'auto' or a float");
      }
      new_params["shift"] = 0.;
    }
    m_ia_si = make_shared_from_args<CoreInteraction, double, double, double,
                                    double, double,
#ifdef LJGEN_SOFTCORE
                                    double, double,
#endif
                                    double, double, double, double>(
        new_params, "epsilon", "sigma", "cutoff", "shift", "offset",
#ifdef LJGEN_SOFTCORE
        "lam", "delta",
#endif
        "e1", "e2", "b1", "b2");
    if (shift_string != nullptr) {
      m_ia_si->shift = m_ia_si->get_auto_shift();
    }
  }
};
#endif // LENNARD_JONES_GENERIC

#ifdef LJCOS
class InteractionLJcos
    : public InteractionPotentialInterface<::LJcos_Parameters> {
protected:
  CoreInteraction IA_parameters::*get_ptr_offset() const override {
    return &::IA_parameters::ljcos;
  }

public:
  InteractionLJcos() {
    add_parameters({
        make_autoparameter(&CoreInteraction::eps, "epsilon"),
        make_autoparameter(&CoreInteraction::sig, "sigma"),
        make_autoparameter(&CoreInteraction::cut, "cutoff"),
        make_autoparameter(&CoreInteraction::offset, "offset"),
    });
  }

  void make_new_instance(VariantMap const &params) override {
    m_ia_si =
        make_shared_from_args<CoreInteraction, double, double, double, double>(
            params, "epsilon", "sigma", "cutoff", "offset");
  }
};
#endif // LJCOS

#ifdef LJCOS2
class InteractionLJcos2
    : public InteractionPotentialInterface<::LJcos2_Parameters> {
protected:
  CoreInteraction IA_parameters::*get_ptr_offset() const override {
    return &::IA_parameters::ljcos2;
  }

public:
  InteractionLJcos2() {
    add_parameters({
        make_autoparameter(&CoreInteraction::eps, "epsilon"),
        make_autoparameter(&CoreInteraction::sig, "sigma"),
        make_autoparameter(&CoreInteraction::offset, "offset"),
        make_autoparameter(&CoreInteraction::w, "width"),
    });
  }

  void make_new_instance(VariantMap const &params) override {
    m_ia_si =
        make_shared_from_args<CoreInteraction, double, double, double, double>(
            params, "epsilon", "sigma", "offset", "width");
  }

  Variant do_call_method(std::string const &name,
                         VariantMap const &params) override {
    if (name == "get_cutoff") {
      return m_ia_si.get()->cut;
    }
    return InteractionPotentialInterface<CoreInteraction>::do_call_method(
        name, params);
  }
};
#endif // LJCOS2

#ifdef HERTZIAN
class InteractionHertzian
    : public InteractionPotentialInterface<::Hertzian_Parameters> {
protected:
  CoreInteraction IA_parameters::*get_ptr_offset() const override {
    return &::IA_parameters::hertzian;
  }

public:
  InteractionHertzian() {
    add_parameters({
        make_autoparameter(&CoreInteraction::eps, "eps"),
        make_autoparameter(&CoreInteraction::sig, "sig"),
    });
  }
  void make_new_instance(VariantMap const &params) override {
    m_ia_si = make_shared_from_args<CoreInteraction, double, double>(
        params, "eps", "sig");
  }
};
#endif // HERTZIAN

#ifdef GAUSSIAN
class InteractionGaussian
    : public InteractionPotentialInterface<::Gaussian_Parameters> {
protected:
  CoreInteraction IA_parameters::*get_ptr_offset() const override {
    return &::IA_parameters::gaussian;
  }

public:
  InteractionGaussian() {
    add_parameters({
        make_autoparameter(&CoreInteraction::eps, "eps"),
        make_autoparameter(&CoreInteraction::sig, "sig"),
        make_autoparameter(&CoreInteraction::cut, "cutoff"),
    });
  }
  void make_new_instance(VariantMap const &params) override {
    m_ia_si = make_shared_from_args<CoreInteraction, double, double, double>(
        params, "eps", "sig", "cutoff");
  }
};
#endif // GAUSSIAN

class NonBondedInteractionHandle
    : public AutoParameters<NonBondedInteractionHandle> {
  std::array<int, 2> m_types = {-1, -1};
  std::shared_ptr<::IA_parameters> m_interaction;
#ifdef WCA
  std::shared_ptr<InteractionWCA> m_wca;
#endif
#ifdef LENNARD_JONES
  std::shared_ptr<InteractionLJ> m_lj;
#endif
#ifdef LENNARD_JONES_GENERIC
  std::shared_ptr<InteractionLJGen> m_ljgen;
#endif
#ifdef LJCOS
  std::shared_ptr<InteractionLJcos> m_ljcos;
#endif
#ifdef LJCOS2
  std::shared_ptr<InteractionLJcos2> m_ljcos2;
#endif
#ifdef HERTZIAN
  std::shared_ptr<InteractionHertzian> m_hertzian;
#endif
#ifdef GAUSSIAN
  std::shared_ptr<InteractionGaussian> m_gaussian;
#endif

  template <class T>
  auto make_autoparameter(std::shared_ptr<T> &member, const char *key) const {
    auto const setter = [this, &member](Variant const &v) {
      member = get_value<std::shared_ptr<T>>(v);
      if (m_types[0] != -1) {
        auto const types = Variant{std::vector<int>{{m_types[0], m_types[1]}}};
        member->do_call_method("bind_types", VariantMap{{"_types", types}});
        member->copy_si_to_core();
        on_non_bonded_ia_change();
      }
    };
    return AutoParameter{key, setter, [&member]() { return member; }};
  }

public:
  NonBondedInteractionHandle() {
    add_parameters({
#ifdef WCA
        make_autoparameter(m_wca, "wca"),
#endif
#ifdef LENNARD_JONES
        make_autoparameter(m_lj, "lennard_jones"),
#endif
#ifdef LENNARD_JONES_GENERIC
        make_autoparameter(m_ljgen, "generic_lennard_jones"),
#endif
#ifdef LJCOS
        make_autoparameter(m_ljcos, "lennard_jones_cos"),
#endif
#ifdef LJCOS2
        make_autoparameter(m_ljcos2, "lennard_jones_cos2"),
#endif
#ifdef HERTZIAN
        make_autoparameter(m_hertzian, "hertzian"),
#endif
#ifdef GAUSSIAN
        make_autoparameter(m_gaussian, "gaussian"),
#endif
    });
  }

private:
  template <class T>
  void set_member(std::shared_ptr<T> &member, std::string key,
                  std::string so_name, VariantMap const &params) {
    auto const ia_types = VariantMap{{"_types", params.at("_types")}};
    if (params.count(key) != 0) {
      member = get_value<std::shared_ptr<T>>(params.at(key));
      member->do_call_method("bind_types", ia_types);
      member->copy_si_to_core();
    } else {
      auto so_object = context()->make_shared_local(so_name, ia_types);
      member = std::dynamic_pointer_cast<T>(so_object);
      member->copy_core_to_si();
    }
  }

public:
  Variant do_call_method(std::string const &name,
                         VariantMap const &params) override {
    if (name == "get_types") {
      return std::vector<int>{{m_types[0], m_types[1]}};
    }
    return {};
  }

  void do_construct(VariantMap const &params) override {
    assert(params.count("_types") != 0);
    auto const types = get_value<std::vector<int>>(params.at("_types"));
    m_types[0] = std::min(types[0], types[1]);
    m_types[1] = std::max(types[0], types[1]);
    // make type exist
    mpi_realloc_ia_params_local(m_types[1] + 1);
    // create interface objects
    auto const key = get_ia_param_key(m_types[0], m_types[1]);
    m_interaction = ::nonbonded_ia_params[key];
#ifdef WCA
    set_member<InteractionWCA>(m_wca, "wca", "Interactions::InteractionWCA",
                               params);
#endif
#ifdef LENNARD_JONES
    set_member<InteractionLJ>(m_lj, "lennard_jones",
                              "Interactions::InteractionLJ", params);
#endif
#ifdef LENNARD_JONES_GENERIC
    set_member<InteractionLJGen>(m_ljgen, "generic_lennard_jones",
                                 "Interactions::InteractionLJGen", params);
#endif
#ifdef LJCOS
    set_member<InteractionLJcos>(m_ljcos, "lennard_jones_cos",
                                 "Interactions::InteractionLJcos", params);
#endif
#ifdef LJCOS2
    set_member<InteractionLJcos2>(m_ljcos2, "lennard_jones_cos2",
                                  "Interactions::InteractionLJcos2", params);
#endif
#ifdef HERTZIAN
    set_member<InteractionHertzian>(
        m_hertzian, "hertzian", "Interactions::InteractionHertzian", params);
#endif
#ifdef GAUSSIAN
    set_member<InteractionGaussian>(
        m_gaussian, "gaussian", "Interactions::InteractionGaussian", params);
#endif
  }

  auto get_ia() const { return m_interaction; }
};

} // namespace Interactions
} // namespace ScriptInterface

#endif

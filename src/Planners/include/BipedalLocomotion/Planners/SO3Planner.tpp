/**
 * @file SO3Planner.tpp
 * @authors Giulio Romualdi
 * @copyright 2020 Istituto Italiano di Tecnologia (IIT). This software may be modified and
 * distributed under the terms of the BSD-3-Clause license.
 */

#ifndef BIPEDAL_LOCOMOTION_PLANNERS_SO3_PLANNER_TPP
#define BIPEDAL_LOCOMOTION_PLANNERS_SO3_PLANNER_TPP

#include <iostream>
#include <chrono>

#include <manif/SO3.h>

#include <BipedalLocomotion/Planners/SO3Planner.h>
#include <BipedalLocomotion/TextLogging/Logger.h>

namespace BipedalLocomotion
{
namespace Planners
{

template <class Rep, class Period>
constexpr double durationToSeconds(const std::chrono::duration<Rep, Period>& d)
{
    return std::chrono::duration<double>(d).count();
}

template <LieGroupTrivialization trivialization>
bool SO3Planner<trivialization>::setRotations(const manif::SO3d& initialRotation,
                                              const manif::SO3d& finalRotation,
                                              const std::chrono::nanoseconds& duration)
{

    constexpr auto logPrefix = "[SO3Planner::setRotation]";

    if (duration <= std::chrono::nanoseconds::zero())
    {
        log()->error("{} The trajectory duration must be a strictly positive number.", logPrefix);
        return false;
    }

    m_initialRotation = initialRotation;
    m_T = duration;

    if constexpr (trivialization == LieGroupTrivialization::Right)
    {
        m_distance = (finalRotation * initialRotation.inverse()).log();
    } else
    // Please read it as trivialization == LieGroupTrivialization::Left
    {
        m_distance = (initialRotation.inverse() * finalRotation).log();
    }

    // reset the advance current time
    m_advanceCurrentTime = std::chrono::nanoseconds::zero();

    // reset the state
    m_state.rotation = m_initialRotation;
    m_state.velocity.setZero();
    m_state.acceleration.setZero();

    return true;
}

template <LieGroupTrivialization trivialization>
template <class Derived>
bool SO3Planner<trivialization>::evaluatePoint(const std::chrono::nanoseconds& time,
                                               manif::SO3d& rotation,
                                               manif::SO3TangentBase<Derived>& velocity,
                                               manif::SO3TangentBase<Derived>& acceleration) const
{
    constexpr auto logPrefix = "[SO3Planner::evaluatePoint]";
    if (time < std::chrono::nanoseconds::zero() || time > m_T)
    {
        log()->error("{} The time has to be a real positive number between 0 and {}.",
                     logPrefix,
                     m_T);
        return false;
    }

    const double t = durationToSeconds(time);
    const double T = durationToSeconds(m_T);
    // Compute the displacement in the tangent space
    // - displacement_tangent = log(finalRotation * initialRotation.inverse()) * s(t) (in case of
    //                                                                                 Right
    //                                                                                 Trivialization)
    // - displacement_tangent = log(initialRotation.inverse() * finalRotation) * s(t) (in case of
    //                                                                                 Left
    //                                                                                 Trivialization)
    // You can find the computation of s in "Modern Robotics: Mechanics, Planning, and Control"
    // (Chapter 9.2)
    // TODO (Giulio) avoid to use t / T you may use std::chrono::ratio
    const double s = 10 * std::pow(t / T, 3)
                     - 15 * std::pow(t / T, 4)
                     + 6 * std::pow(t / T, 5);
    const manif::SO3d::Tangent displacementTangent = m_distance * s;

    if constexpr (trivialization == LieGroupTrivialization::Right)
    {
        // please read this as R(t) = exp(displacementTangent) * initialRotation
        rotation = displacementTangent.exp() * m_initialRotation;
    } else
    // Please read it as trivialization == LieGroupTrivialization::Left
    {
        // please read this as R(t) = initialRotation * exp(displacementTangent)
        rotation = m_initialRotation * displacementTangent.exp();
    }

    // compute velocity (it is expressed in body / inertial frame accordingly to the chosen
    // trivialization)
    // You can find the computation of sDot in "Modern Robotics: Mechanics, Planning, and Control"
    // (Chapter 9.2)
    const double sDot = 10 * 3 * std::pow(t, 2) / std::pow(T, 3)
                        - 15 * 4 * std::pow(t, 3) / std::pow(T, 4)
                        + 6 * 5 * std::pow(t, 4) / std::pow(T, 5);
    velocity = m_distance * sDot;

    // compute acceleration (it is expressed in body / inertial frame accordingly to the chosen
    // trivialization)
    // You can find the computation of sDdot in "Modern Robotics: Mechanics,
    // Planning, and Control" (Chapter 9.2)
    const double sDdot = 10 * 3 * 2 * t / std::pow(T, 3)
                         - 15 * 4 * 3 * std::pow(t, 2) / std::pow(T, 4)
                         + 6 * 5 * 4 * std::pow(t, 3) / std::pow(T, 5);
    acceleration = m_distance * sDdot;

    return true;
}

template <LieGroupTrivialization trivialization>
bool SO3Planner<trivialization>::evaluatePoint(const std::chrono::nanoseconds& time,
                                               SO3PlannerState& state) const
{
    return this->evaluatePoint(time, state.rotation, state.velocity, state.acceleration);
}

template <LieGroupTrivialization trivialization>
bool SO3Planner<trivialization>::setAdvanceTimeStep(const std::chrono::nanoseconds& dt)
{
    constexpr auto logPrefix = "[SO3Planner::setAdvanceTimeStep]";
    if (dt <= std::chrono::nanoseconds::zero())
    {
        log()->error("{} The time step of the advance time has to be a strictly positive number.",
                     logPrefix);
        return false;
    }

    m_advanceTimeStep = dt;

    return true;
}

template <LieGroupTrivialization trivialization>
bool SO3Planner<trivialization>::isOutputValid() const
{
    // if the time step is different from zero
    return (m_advanceTimeStep != std::chrono::nanoseconds::zero());
}

template <LieGroupTrivialization trivialization> bool SO3Planner<trivialization>::advance()
{
    constexpr auto logPrefix = "[SO3Planner::advance]";
    if (!this->isOutputValid())
    {
        log()->error("{} The advance capabilities cannot be used. Did you set the advance time "
                     "step?",
                     logPrefix);
        return false;
    }

    // advance the time step
    m_advanceCurrentTime = std::min(m_advanceTimeStep + m_advanceCurrentTime, m_T);

    // update the state of the system
    return evaluatePoint(m_advanceCurrentTime, m_state);
}

template <LieGroupTrivialization trivialization>
const SO3PlannerState& SO3Planner<trivialization>::getOutput() const
{
    return m_state;
}

} // namespace Planners
} // namespace BipedalLocomotion

#endif // BIPEDAL_LOCOMOTION_PLANNERS_SO3_PLANNER_TPP

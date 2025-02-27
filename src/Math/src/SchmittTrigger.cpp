/**
 * @file SchmittTrigger.cpp
 * @authors Giulio Romualdi
 * @copyright 2023 Istituto Italiano di Tecnologia (IIT). This software may be modified and
 * distributed under the terms of the BSD-3-Clause license.
 */

#include <BipedalLocomotion/Math/SchmittTrigger.h>
#include <BipedalLocomotion/TextLogging/Logger.h>
#include <chrono>

using namespace BipedalLocomotion::Math;

bool SchmittTrigger::initialize(std::weak_ptr<const ParametersHandler::IParametersHandler> handler)
{
    constexpr auto logPrefix = "[SchmittTrigger::initialize]";

    auto ptr = handler.lock();
    if (ptr == nullptr)
    {
        log()->error("{} Invalid parameter handler.", logPrefix);
        return false;
    }

    auto populateparameter
        = [logPrefix, ptr](const std::string& paramName, auto& parameter) -> bool {
        if (!ptr->getParameter(paramName, parameter))
        {
            log()->error("{} Unable to find the parameter '{}'", logPrefix, paramName);
            return false;
        }
        return true;
    };

    Params params;
    bool ok = populateparameter("on_threshold", params.onThreshold);
    ok = ok && populateparameter("off_threshold", params.offThreshold);
    ok = ok && populateparameter("switch_on_after", params.switchOnAfter);
    ok = ok && populateparameter("switch_off_after", params.switchOffAfter);

    return ok && this->initialize(params);
}

bool SchmittTrigger::initialize(const Params& params)
{

    constexpr auto logPrefix = "[SchmittTrigger::initialize]";

    if (params.offThreshold > params.onThreshold)
    {
        log()->error("{} The ON threshold must be greater than the OFF threshold", logPrefix);
        return false;
    }

    m_params = params;

    return true;
}

bool SchmittTrigger::advance()
{
    // if the trigger is not active
    if (!m_state.state)
    {
        // the state is inactive this means that we can reset the fallingEdgeTimeInstant. We can
        // avoid to do this at every instant. However, coping a double is not the bootlneck. :)
        m_fallingDetected = false;

        // check if the input is higher of the threshold
        if (m_input.rawValue >= m_params.onThreshold)
        {
            // We detected a rising edge
            if (!m_risingDetected)
            {
                m_risingEdgeTimeInstant = m_input.time;
                m_risingDetected = true;
            }

            // integrate the timer
            m_timer = m_input.time - m_risingEdgeTimeInstant;

            // if the timer is greater than a threshold is time to switch
            if (m_timer >= m_params.switchOnAfter)
            {
                m_state.state = true;
                m_state.switchTime = m_input.time;
                m_state.edgeTime = m_risingEdgeTimeInstant;
                m_timer = std::chrono::nanoseconds::zero();
            }
        }
    } else
    {

        // the state is inactive this means that we can reset the risingEdgeTimeInstant. We can
        // avoid to do this at every instant. However, coping a double is not the bootlneck. :)
        m_risingDetected = false;

        // check if the input is lower of the threshold
        if (m_input.rawValue <= m_params.offThreshold)
        {
            // We detected a falling edge
            if (!m_fallingDetected)
            {
                m_fallingEdgeTimeInstant = m_input.time;
                m_fallingDetected = true;
            }

            // here a small delta is added to the timer
            m_timer = m_input.time - m_fallingEdgeTimeInstant;

            // if the value is lower the threshold for more than switchOffAfter seconds is time to
            // switch!
            if (m_timer >= m_params.switchOffAfter)
            {
                m_state.state = false;
                m_state.switchTime = m_input.time;
                m_state.edgeTime = m_fallingEdgeTimeInstant;
                m_timer = std::chrono::nanoseconds::zero();
            }
        }
    }
    return true;
}

bool SchmittTrigger::isOutputValid() const
{
    return true;
}

bool SchmittTrigger::setInput(const SchmittTriggerInput& input)
{
    m_input = input;
    return true;
}

void SchmittTrigger::setState(const SchmittTriggerState& state)
{
    m_state = state;

    // when the state is reset the timer is reset as well
    m_timer = std::chrono::nanoseconds::zero();
}

const SchmittTriggerState& SchmittTrigger::getOutput() const
{
    return m_state;
}

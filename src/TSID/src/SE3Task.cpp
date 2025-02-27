/**
 * @file SE3Task.cpp
 * @authors Giulio Romualdi
 * @copyright 2020 Istituto Italiano di Tecnologia (IIT). This software may be modified and
 * distributed under the terms of the BSD-3-Clause license.
 */

#include <BipedalLocomotion/Conversions/ManifConversions.h>
#include <BipedalLocomotion/TSID/SE3Task.h>
#include <BipedalLocomotion/TextLogging/Logger.h>

#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/Model/Model.h>

using namespace BipedalLocomotion::ParametersHandler;
using namespace BipedalLocomotion::System;
using namespace BipedalLocomotion::TSID;

bool SE3Task::setVariablesHandler(const System::VariablesHandler& variablesHandler)
{
    if (!m_isInitialized)
    {
        log()->error("[SE3Task::setVariablesHandler] The task is not initialized. Please call "
                     "initialize method.");
        return false;
    }

    // get the variable
    if (!variablesHandler.getVariable(m_robotAccelerationVariable.name,
                                      m_robotAccelerationVariable))
    {
        log()->error("[SE3Task::setVariablesHandler] Unable to get the variable named {}.",
                     m_robotAccelerationVariable.name);
        return false;
    }

    // get the variable
    if (m_robotAccelerationVariable.size
        != m_kinDyn->getNrOfDegreesOfFreedom() + m_spatialVelocitySize)
    {
        log()->error("[SE3Task::setVariablesHandler] The size of the robot velocity variable is "
                     "different from the one expected. Expected size: {}. Given size: {}.",
                     m_kinDyn->getNrOfDegreesOfFreedom() + m_spatialVelocitySize,
                     m_robotAccelerationVariable.size);
        return false;
    }

    // resize the matrices
    m_A.resize(m_spatialVelocitySize, variablesHandler.getNumberOfVariables());
    m_A.setZero();
    m_b.resize(m_spatialVelocitySize);

    return true;
}

bool SE3Task::setKinDyn(std::shared_ptr<iDynTree::KinDynComputations> kinDyn)
{
    if ((kinDyn == nullptr) || (!kinDyn->isValid()))
    {
        log()->error("[SE3Task::setKinDyn] Invalid kinDyn object.");
        return false;
    }

    m_kinDyn = kinDyn;
    return true;
}

bool SE3Task::initialize(std::weak_ptr<const ParametersHandler::IParametersHandler> paramHandler)
{
    constexpr std::string_view errorPrefix = "[SE3Task::initialize] ";

    std::string frameName = "Unknown";
    constexpr auto descriptionPrefix = "SE3Task Optimal Control Element - Frame name: ";

    if(m_kinDyn == nullptr || !m_kinDyn->isValid())
    {
        log()->error("{}, [{} {}] KinDynComputations object is not valid.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    if (m_kinDyn->getFrameVelocityRepresentation()
        != iDynTree::FrameVelocityRepresentation::MIXED_REPRESENTATION)
    {
        log()->error("{}, [{} {}] The task supports only quantities expressed in MIXED "
                     "representation. Please provide a KinDynComputations with Frame velocity "
                     "representation set to MIXED_REPRESENTATION.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    auto ptr = paramHandler.lock();
    if(ptr == nullptr)
    {
        log()->error("{}, [{} {}] The parameter handler is not valid.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    if (!ptr->getParameter("robot_acceleration_variable_name", m_robotAccelerationVariable.name))
    {
        log()->error("{}, [{} {}] Error while retrieving the robot velocity variable.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    if (!ptr->getParameter("frame_name", frameName)
        || (m_frameIndex = m_kinDyn->model().getFrameIndex(frameName))
               == iDynTree::FRAME_INVALID_INDEX)
    {
        log()->error("{}, [{} {}] Error while retrieving the frame that should be controlled.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    // set the gains for the R3 controller
    Eigen::Vector3d kpLinear, kdLinear;
    double bufferLinearScalar;

    if(ptr->getParameter("kp_linear", bufferLinearScalar))
    {
        kpLinear.setConstant(bufferLinearScalar);
    }
    else if(!ptr->getParameter("kp_linear", kpLinear))
    {
        log()->error("{}, [{} {}] Unable to get the proportional linear gain.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    if(ptr->getParameter("kd_linear", bufferLinearScalar))
    {
        kdLinear.setConstant(bufferLinearScalar);
    }
    else if(!ptr->getParameter("kd_linear", kdLinear))
    {
        log()->error("{}, [{} {}] Unable to get the derivative linear gain.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    m_R3Controller.setGains(kpLinear, kdLinear);

    // set the gains for the SO3 controller
    Eigen::Vector3d kpAngular, kdAngular;
    double bufferAngularScalar;

    if(ptr->getParameter("kp_angular", bufferAngularScalar))
    {
        kpAngular.setConstant(bufferAngularScalar);
    }
    else if(!ptr->getParameter("kp_angular", kpAngular))
    {
        log()->error("{}, [{} {}] Unable to get the proportional angular gain.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    if(ptr->getParameter("kd_angular", bufferAngularScalar))
    {
        kdAngular.setConstant(bufferAngularScalar);
    }
    else if(!ptr->getParameter("kd_angular", kdAngular))
    {
        log()->error("{}, [{} {}] Unable to get the derivative angular gain.",
                     errorPrefix,
                     descriptionPrefix,
                     frameName);
        return false;
    }

    m_SO3Controller.setGains(kpAngular, kdAngular);

    // set the description
    m_description = std::string(descriptionPrefix) + frameName + ".";

    m_isInitialized = true;

    return true;
}

bool SE3Task::update()
{
    m_isValid = false;

    if (!m_isInitialized)
    {
        log()->error("[SE3Task::update] Please call initialize() before update().");
        return m_isValid;
    }

    auto getControllerOutupt = [&](const auto& controller) {
        if (m_controllerMode == Mode::Enable)
            return controller.getControl().coeffs();
        else
            return controller.getFeedForward().coeffs();
    };

    m_b = -iDynTree::toEigen(m_kinDyn->getFrameBiasAcc(m_frameIndex));

    m_SO3Controller.setState(BipedalLocomotion::Conversions::toManifRot(
                                 m_kinDyn->getWorldTransform(m_frameIndex).getRotation()),
                             iDynTree::toEigen(
                                 m_kinDyn->getFrameVel(m_frameIndex).getAngularVec3()));

    m_R3Controller.setState(iDynTree::toEigen(
                                m_kinDyn->getWorldTransform(m_frameIndex).getPosition()),
                            iDynTree::toEigen(m_kinDyn->getFrameVel(m_frameIndex).getLinearVec3()));

    // update the controller ouptut
    m_SO3Controller.computeControlLaw();
    m_R3Controller.computeControlLaw();

    // get the output
    m_b.head<3>() += getControllerOutupt(m_R3Controller);
    m_b.tail<3>() += getControllerOutupt(m_SO3Controller);

    if (!m_kinDyn->getFrameFreeFloatingJacobian(m_frameIndex,
                                                this->subA(m_robotAccelerationVariable)))
    {
        log()->error("[SE3Task::update] Unable to get the jacobian.");
        return m_isValid;
    }

    m_isValid = true;
    return m_isValid;
}

bool SE3Task::setSetPoint(const manif::SE3d& I_H_F,
                          const manif::SE3d::Tangent& mixedVelocity,
                          const manif::SE3d::Tangent& mixedAcceleration)
{
    bool ok = true;
    ok = ok && m_R3Controller.setDesiredState(I_H_F.translation(), mixedVelocity.lin());
    ok = ok && m_R3Controller.setFeedForward(mixedAcceleration.lin());

    ok = ok && m_SO3Controller.setDesiredState(I_H_F.quat(), mixedVelocity.ang());
    ok = ok && m_SO3Controller.setFeedForward(mixedAcceleration.ang());

    return ok;
}

std::size_t SE3Task::size() const
{
    return m_spatialVelocitySize;
}

SE3Task::Type SE3Task::type() const
{
    return Type::equality;
}

bool SE3Task::isValid() const
{
    return m_isValid;
}

void SE3Task::setTaskControllerMode(Mode mode)
{
    m_controllerMode = mode;
}

SE3Task::Mode SE3Task::getTaskControllerMode() const
{
    return m_controllerMode;
}

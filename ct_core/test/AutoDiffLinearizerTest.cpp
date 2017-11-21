/**********************************************************************************************************************
This file is part of the Control Toolbox (https://adrlab.bitbucket.io/ct), copyright by ETH Zurich, Google Inc.
Authors:  Michael Neunert, Markus Giftthaler, Markus Stäuble, Diego Pardo, Farbod Farshidian
Licensed under Apache2 license (see LICENSE file in main directory)
**********************************************************************************************************************/

#include <ct/core/core.h>
#include "system/TestNonlinearSystem.h"

// Bring in gtest
#include <gtest/gtest.h>

using namespace ct::core;
using std::shared_ptr;


/*!
 *  \example AutoDiffLinearizerTest.cpp
 *
 *  This unit test serves as example how to use the SystemLinearizer (numerical differentiation) and
 *  the Autodiff-Linearizer (automatic differentiation)
 */
TEST(AutoDiffLinearizerTest, SystemLinearizerComparison)
{
    // define the dimensions of the system
    const size_t state_dim = TestNonlinearSystem::STATE_DIM;
    const size_t control_dim = TestNonlinearSystem::CONTROL_DIM;

    // typedefs for the auto-differentiable system
    typedef CppAD::AD<double> AD_Scalar;
    typedef tpl::TestNonlinearSystem<AD_Scalar> TestNonlinearSystemAD;

    // handy typedefs for the Jacobian
    typedef Eigen::Matrix<double, state_dim, state_dim> A_type;
    typedef Eigen::Matrix<double, state_dim, control_dim> B_type;

    // create two nonlinear systems, one regular one and one auto-differentiable
    double w_n = 100;
    shared_ptr<TestNonlinearSystem> nonlinearSystem(new TestNonlinearSystem(w_n));
    shared_ptr<TestNonlinearSystemAD> nonlinearSystemAD(new tpl::TestNonlinearSystem<AD_Scalar>(AD_Scalar(w_n)));

    // create a linearizer that applies numerical differentiation
    SystemLinearizer<state_dim, control_dim> systemLinearizer(nonlinearSystem);

    // create a linearizer that uses codegeneration
    AutoDiffLinearizer<state_dim, control_dim> adLinearizer(nonlinearSystemAD);
    std::shared_ptr<AutoDiffLinearizer<state_dim, control_dim>> adLinearizerClone(adLinearizer.clone());

    // create state, control and time variables
    StateVector<TestNonlinearSystem::STATE_DIM> x;
    ControlVector<TestNonlinearSystem::CONTROL_DIM> u;
    double t = 0;

    for (size_t i = 0; i < 1000; i++)
    {
        // set a random state
        x.setRandom();
        u.setRandom();

        // use the numerical differentiation linearizer
        A_type A_system = systemLinearizer.getDerivativeState(x, u, t);
        B_type B_system = systemLinearizer.getDerivativeControl(x, u, t);

        // use the auto differentiation linearzier
        A_type A_ad = adLinearizer.getDerivativeState(x, u, t);
        B_type B_ad = adLinearizer.getDerivativeControl(x, u, t);

        A_type A_adCloned = adLinearizerClone->getDerivativeState(x, u, t);
        B_type B_adCloned = adLinearizerClone->getDerivativeControl(x, u, t);

        // verify the result
        ASSERT_LT((A_system - A_ad).array().abs().maxCoeff(), 1e-5);
        ASSERT_LT((B_system - B_ad).array().abs().maxCoeff(), 1e-5);

        ASSERT_LT((A_system - A_adCloned).array().abs().maxCoeff(), 1e-5);
        ASSERT_LT((B_system - B_adCloned).array().abs().maxCoeff(), 1e-5);
    }
}


TEST(AutoDiffLinearizerTestMP, SystemLinearizerComparisonMP)
{
    // define the dimensions of the system
    const size_t state_dim = TestNonlinearSystem::STATE_DIM;
    const size_t control_dim = TestNonlinearSystem::CONTROL_DIM;
    typedef std::shared_ptr<AutoDiffLinearizer<state_dim, control_dim>> AdLinearizerPtr;
    typedef StateVector<state_dim> StateVector;
    typedef ControlVector<control_dim> ControlVector;

    // typedefs for the auto-differentiable system
    typedef CppAD::AD<double> AD_Scalar;
    typedef tpl::TestNonlinearSystem<AD_Scalar> TestNonlinearSystemAD;

    // handy typedefs for the Jacobian
    typedef Eigen::Matrix<double, state_dim, state_dim> A_type;
    typedef Eigen::Matrix<double, state_dim, control_dim> B_type;

    // create two nonlinear systems, one regular one and one auto-differentiable
    double w_n = 100;
    shared_ptr<TestNonlinearSystem> nonlinearSystem(new TestNonlinearSystem(w_n));
    shared_ptr<TestNonlinearSystemAD> nonlinearSystemAD(new tpl::TestNonlinearSystem<AD_Scalar>(AD_Scalar(w_n)));

    // create a linearizer that applies numerical differentiation
    SystemLinearizer<state_dim, control_dim> systemLinearizer(nonlinearSystem);
    AutoDiffLinearizer<state_dim, control_dim> adLinearizer(nonlinearSystemAD);

    std::vector<std::shared_ptr<SystemLinearizer<state_dim, control_dim>>> systemLinearizers;

    size_t runs = 1000;
    size_t numThreads = 5;

    // The ad objects cannot yet be initialized here
    for (size_t i = 0; i < numThreads; ++i)
        systemLinearizers.push_back(
            std::shared_ptr<SystemLinearizer<state_dim, control_dim>>(systemLinearizer.clone()));

    // Count in the main thread
    CppadParallel::initParallel(numThreads + 1);
    for (size_t n = 0; n < runs; ++n)
    {
        std::vector<std::thread> threads;

        for (size_t i = 0; i < numThreads; ++i)
        {
            threads.push_back(std::thread([i, state_dim, control_dim, &adLinearizer, &systemLinearizers]() {
                // The ad objects are initialized here, because they need to be associated with the specfic thread number
                AdLinearizerPtr adLinearizerLocal = AdLinearizerPtr(adLinearizer.clone());

                StateVector x;
                ControlVector u;
                double t = 0.0;

                x.setRandom();
                u.setRandom();

                // use the numerical differentiation linearizer
                A_type A_system = systemLinearizers[i]->getDerivativeState(x, u, t);
                B_type B_system = systemLinearizers[i]->getDerivativeControl(x, u, t);

                // use the auto differentiation linearzier
                A_type A_ad = adLinearizerLocal->getDerivativeState(x, u, t);
                B_type B_ad = adLinearizerLocal->getDerivativeControl(x, u, t);

                // verify the result
                ASSERT_LT((A_system - A_ad).array().abs().maxCoeff(), 1e-5);
                ASSERT_LT((B_system - B_ad).array().abs().maxCoeff(), 1e-5);
            }));
        }

        for (auto& thr : threads)
            thr.join();
    }

    CppadParallel::resetParallel();
}


int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

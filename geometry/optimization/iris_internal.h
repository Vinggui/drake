#pragma once
/*
 This file contains the internal functions used by iris.cc. The users shouldn't
 call these functions, but we expose them for unit tests.
 */

#include <memory>
#include <optional>
#include <variant>

#include "drake/geometry/optimization/affine_ball.h"
#include "drake/geometry/optimization/cartesian_product.h"
#include "drake/geometry/optimization/convex_set.h"
#include "drake/geometry/optimization/hyperellipsoid.h"
#include "drake/geometry/optimization/iris_internal.h"
#include "drake/geometry/optimization/minkowski_sum.h"
#include "drake/geometry/optimization/vpolytope.h"
#include "drake/multibody/plant/multibody_plant.h"
#include "drake/solvers/mathematical_program.h"
#include "drake/solvers/solver_interface.h"

namespace drake {
namespace geometry {
namespace optimization {
namespace internal {
/* Takes q, p_AA, and p_BB and enforces that p_WA == p_WB.
 */
class SamePointConstraint : public solvers::Constraint {
 public:
  DRAKE_NO_COPY_NO_MOVE_NO_ASSIGN(SamePointConstraint);

  SamePointConstraint(const multibody::MultibodyPlant<double>* plant,
                      const systems::Context<double>& context);

  ~SamePointConstraint() override;

  void set_frameA(const multibody::Frame<double>* frame) { frameA_ = frame; }

  void set_frameB(const multibody::Frame<double>* frame) { frameB_ = frame; }

  void EnableSymbolic();

 private:
  void DoEval(const Eigen::Ref<const Eigen::VectorXd>& x,
              Eigen::VectorXd* y) const override;

  void DoEval(const Eigen::Ref<const AutoDiffVecXd>& x,
              AutoDiffVecXd* y) const override;

  void DoEval(const Eigen::Ref<const VectorX<symbolic::Variable>>& x,
              VectorX<symbolic::Expression>* y) const override;

  const multibody::MultibodyPlant<double>* const plant_;
  const multibody::Frame<double>* frameA_{nullptr};
  const multibody::Frame<double>* frameB_{nullptr};
  std::unique_ptr<systems::Context<double>> context_;

  std::unique_ptr<multibody::MultibodyPlant<symbolic::Expression>>
      symbolic_plant_{nullptr};
  std::unique_ptr<systems::Context<symbolic::Expression>> symbolic_context_{
      nullptr};
};

// Takes q, p_AA, and p_BB, and enforces that ||p_WA - p_WB||² <= d², where d is
// a user-defined maximum distance.
class PointsBoundedDistanceConstraint : public solvers::Constraint {
 public:
  DRAKE_NO_COPY_NO_MOVE_NO_ASSIGN(PointsBoundedDistanceConstraint);

  PointsBoundedDistanceConstraint(
      const multibody::MultibodyPlant<double>* plant,
      const systems::Context<double>& context, const double max_distance);

  ~PointsBoundedDistanceConstraint() override;

  void set_frameA(const multibody::Frame<double>* frame) {
    same_point_constraint_.set_frameA(frame);
  }

  void set_frameB(const multibody::Frame<double>* frame) {
    same_point_constraint_.set_frameB(frame);
  }

  void set_max_distance(const double max_distance) {
    UpdateUpperBound(Vector1d(max_distance * max_distance));
  }

  void EnableSymbolic() { same_point_constraint_.EnableSymbolic(); }

 private:
  void DoEval(const Eigen::Ref<const Eigen::VectorXd>& x,
              Eigen::VectorXd* y) const override;

  void DoEval(const Eigen::Ref<const AutoDiffVecXd>& x,
              AutoDiffVecXd* y) const override;

  void DoEval(const Eigen::Ref<const VectorX<symbolic::Variable>>& x,
              VectorX<symbolic::Expression>* y) const override;

  geometry::optimization::internal::SamePointConstraint same_point_constraint_;
};

/* Defines a MathematicalProgram to solve the problem
 min_q (q-d) CᵀC (q-d)
 s.t. setA in frameA and setB in frameB are in collision in q.
      Aq ≤ b.
 where C, d are the matrix and center from the hyperellipsoid E.

 The class design supports repeated solutions of the (nearly) identical
 problem from different initial guesses.
 */
class ClosestCollisionProgram {
 public:
  ClosestCollisionProgram(
      std::variant<std::shared_ptr<SamePointConstraint>,
                   std::shared_ptr<PointsBoundedDistanceConstraint>>
          same_point_constraint,
      const multibody::Frame<double>& frameA,
      const multibody::Frame<double>& frameB, const ConvexSet& setA,
      const ConvexSet& setB, const Hyperellipsoid& E,
      const Eigen::Ref<const Eigen::MatrixXd>& A,
      const Eigen::Ref<const Eigen::VectorXd>& b);

  ~ClosestCollisionProgram();

  void UpdatePolytope(const Eigen::Ref<const Eigen::MatrixXd>& A,
                      const Eigen::Ref<const Eigen::VectorXd>& b);

  // Returns true iff a collision is found.
  // Sets `closest` to an optimizing solution q*, if a solution is found.
  bool Solve(const solvers::SolverInterface& solver,
             const Eigen::Ref<const Eigen::VectorXd>& q_guess,
             const std::optional<solvers::SolverOptions>& solver_options,
             Eigen::VectorXd* closest);

 private:
  solvers::MathematicalProgram prog_;
  solvers::VectorXDecisionVariable q_;
  std::optional<solvers::Binding<solvers::LinearConstraint>> P_constraint_{};
};

// Constructs a ConvexSet for each supported Shape and adds it to the set.
class IrisConvexSetMaker final : public ShapeReifier {
 public:
  DRAKE_NO_COPY_NO_MOVE_NO_ASSIGN(IrisConvexSetMaker);

  IrisConvexSetMaker(const QueryObject<double>& query,
                     std::optional<FrameId> reference_frame)
      : query_{query}, reference_frame_{reference_frame} {};

  void set_reference_frame(const FrameId& reference_frame) {
    DRAKE_DEMAND(reference_frame.is_valid());
    *reference_frame_ = reference_frame;
  }

  void set_geometry_id(const GeometryId& geom_id) { geom_id_ = geom_id; }

  using ShapeReifier::ImplementGeometry;

  void ImplementGeometry(const Box&, void* data);

  void ImplementGeometry(const Capsule&, void* data);

  void ImplementGeometry(const Cylinder&, void* data);

  void ImplementGeometry(const Ellipsoid&, void* data);

  void ImplementGeometry(const HalfSpace&, void* data);

  void ImplementGeometry(const Sphere&, void* data);

  void ImplementGeometry(const Convex&, void* data);

  void ImplementGeometry(const Mesh&, void* data);

 private:
  const QueryObject<double>& query_{};
  std::optional<FrameId> reference_frame_{};
  GeometryId geom_id_{};
};

struct GeometryPairWithDistance {
  GeometryId geomA;
  GeometryId geomB;
  double distance;

  GeometryPairWithDistance(GeometryId gA, GeometryId gB, double dist)
      : geomA(gA), geomB(gB), distance(dist) {}

  bool operator<(const GeometryPairWithDistance& other) const {
    return distance < other.distance;
  }
};

}  // namespace internal
}  // namespace optimization
}  // namespace geometry
}  // namespace drake

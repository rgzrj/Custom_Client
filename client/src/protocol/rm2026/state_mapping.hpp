#pragma once

#include "core/rm2026_state.hpp"
#include <QJsonObject>

#ifdef RM27_UPSTREAM_BACKEND
namespace robomaster {
#else
namespace rm2026::v2 {
#endif
class RobotStaticStatus;
class RobotDynamicStatus;
class GlobalUnitStatus;
class GlobalLogisticsStatus;
class GlobalSpecialMechanism;
class RobotInjuryStat;
class RobotPathPlanInfo;
class MapClickInfo;
class CustomByteBlock;
class GameStatus;
class PenaltyInfo;
class RobotModuleStatus;
class RobotRespawnStatus;
class Event;
class RobotPosition;
class Buff;
class RobotPerformanceSelectionSync;
class DeployModeStatusSync;
class TechCoreMotionStateSync;
class RuneStatusSync;
class SentryStatusSync;
class DartSelectTargetStatusSync;
class AirSupportStatusSync;
class SentryCtrlResult;
}

namespace rm27::protocol::rm2026 {
#ifdef RM27_UPSTREAM_BACKEND
namespace wire = ::robomaster;
#else
namespace wire = ::rm2026::v2;
#endif
// Adapter-only conversion of validated messages; domain types contain no Protobuf/Qt.
historical::rm2026::SelfConfig mapConfig(const wire::RobotStaticStatus& message);
historical::rm2026::SelfDynamic mapDynamic(const wire::RobotDynamicStatus& message);
historical::rm2026::Units mapUnits(const wire::GlobalUnitStatus& message);
historical::rm2026::Economy mapEconomy(const wire::GlobalLogisticsStatus& message);
historical::rm2026::SpecialMechanisms mapSpecialMechanisms(const wire::GlobalSpecialMechanism& message);
historical::rm2026::InjuryStat mapInjury(const wire::RobotInjuryStat& message);
historical::rm2026::PathPlan mapPathPlan(const wire::RobotPathPlanInfo& message);
historical::rm2026::MapClickReport mapMapClick(const wire::MapClickInfo& message);
historical::rm2026::CustomBytes mapCustomBytes(const wire::CustomByteBlock& message);
historical::rm2026::Game mapGame(const wire::GameStatus& message);
historical::rm2026::Penalty mapPenalty(const wire::PenaltyInfo& message);
historical::rm2026::Modules mapModules(const wire::RobotModuleStatus& message);
historical::rm2026::Respawn mapRespawn(const wire::RobotRespawnStatus& message);
historical::rm2026::EventReport mapEvent(const wire::Event& message);
historical::rm2026::Position mapPosition(const wire::RobotPosition& message);
historical::rm2026::BuffReport mapBuff(const wire::Buff& message);
historical::rm2026::Performance mapPerformance(const wire::RobotPerformanceSelectionSync& message);
historical::rm2026::Deployment mapDeployment(const wire::DeployModeStatusSync& message);
historical::rm2026::TechCore mapTechCore(const wire::TechCoreMotionStateSync& message);
historical::rm2026::RuneStatus mapRune(const wire::RuneStatusSync& message);
historical::rm2026::SentryStatus mapSentryStatus(const wire::SentryStatusSync& message);
historical::rm2026::DartStatus mapDartStatus(const wire::DartSelectTargetStatusSync& message);
historical::rm2026::AirSupport mapAirSupport(const wire::AirSupportStatusSync& message);
historical::rm2026::SentryResult mapSentryResult(const wire::SentryCtrlResult& message);
QJsonObject snapshotJson(const historical::rm2026::Snapshot& snapshot);
} // namespace rm27::protocol::rm2026

#pragma once

#include "core/rm2026_state.hpp"
#include <QJsonObject>

namespace rm2026::v2 {
class RobotStaticStatus;
class RobotDynamicStatus;
class GlobalUnitStatus;
class GlobalLogisticsStatus;
class GameStatus;
class PenaltyInfo;
class RobotModuleStatus;
class RobotRespawnStatus;
class Event;
class RobotPosition;
class Buff;
class RobotPerformanceSelectionSync;
class DeployModeStatusSync;
}

namespace rm27::protocol::rm2026 {
// Adapter-only conversion of validated messages; domain types contain no Protobuf/Qt.
historical::rm2026::SelfConfig mapConfig(const ::rm2026::v2::RobotStaticStatus& message);
historical::rm2026::SelfDynamic mapDynamic(const ::rm2026::v2::RobotDynamicStatus& message);
historical::rm2026::Units mapUnits(const ::rm2026::v2::GlobalUnitStatus& message);
historical::rm2026::Economy mapEconomy(const ::rm2026::v2::GlobalLogisticsStatus& message);
historical::rm2026::Game mapGame(const ::rm2026::v2::GameStatus& message);
historical::rm2026::Penalty mapPenalty(const ::rm2026::v2::PenaltyInfo& message);
historical::rm2026::Modules mapModules(const ::rm2026::v2::RobotModuleStatus& message);
historical::rm2026::Respawn mapRespawn(const ::rm2026::v2::RobotRespawnStatus& message);
historical::rm2026::EventReport mapEvent(const ::rm2026::v2::Event& message);
historical::rm2026::Position mapPosition(const ::rm2026::v2::RobotPosition& message);
historical::rm2026::BuffReport mapBuff(const ::rm2026::v2::Buff& message);
historical::rm2026::Performance mapPerformance(const ::rm2026::v2::RobotPerformanceSelectionSync& message);
historical::rm2026::Deployment mapDeployment(const ::rm2026::v2::DeployModeStatusSync& message);
QJsonObject snapshotJson(const historical::rm2026::Snapshot& snapshot);
} // namespace rm27::protocol::rm2026

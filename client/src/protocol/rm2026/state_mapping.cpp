#include "protocol/rm2026/state_mapping.hpp"
#include "client.pb.h"
#include <QJsonArray>

namespace rm27::protocol::rm2026 {
namespace domain = historical::rm2026;
namespace {
domain::OutpostLife outpostLife(const domain::Structure& outpost)
{
    // PDF 53-54: 0..2 invincible/alive, 3..5 destroyed/rebuilding.
    if (outpost.status && *outpost.status > 5) return domain::OutpostLife::Unknown;
    if (outpost.status && outpost.health &&
        ((*outpost.status <= 2) != (*outpost.health > 0))) return domain::OutpostLife::Unknown;
    if (outpost.status) return *outpost.status <= 2 ? domain::OutpostLife::Alive : domain::OutpostLife::Destroyed;
    if (outpost.health) return *outpost.health > 0 ? domain::OutpostLife::Alive : domain::OutpostLife::Destroyed;
    return domain::OutpostLife::Unknown;
}

template<class T> void put(QJsonObject& json, const char* key, const std::optional<T>& value)
{
    if (value) json.insert(key, double(*value));
}
domain::Module module(std::uint32_t code)
{
    return {code, code <= 2 ? static_cast<domain::ModuleState>(code) : domain::ModuleState::Unknown};
}
void putModule(QJsonObject& json, const char* key, const domain::Module& value)
{
    if (!value.code) return;
    constexpr std::array states{"offline", "online", "installation_offline"};
    const int state = static_cast<int>(value.state);
    json.insert(key, QJsonObject{{"code", double(*value.code)},
        {"state", state >= 0 && state < int(states.size()) ? states[state] : "unknown"}});
}
QJsonObject structureJson(const domain::Structure& structure, bool outpost)
{
    QJsonObject json;
    put(json, "health", structure.health);
    put(json, "status", structure.status);
    put(json, "shield", structure.shield);
    if (outpost) json.insert("life", structure.life == domain::OutpostLife::Alive ? "alive" :
        structure.life == domain::OutpostLife::Destroyed ? "destroyed" : "unknown");
    return json;
}
QJsonObject sideJson(const domain::SideUnits& side)
{
    QJsonArray robots;
    for (const auto& robot : side.robots) {
        QJsonObject json{{"role_id", int(robot.roleId)}};
        put(json, "health", robot.health);
        robots.append(json);
    }
    return {{"base", structureJson(side.base, false)},
        {"outpost", structureJson(side.outpost, true)}, {"robots", robots}};
}
} // namespace

domain::SelfConfig mapConfig(const ::rm2026::v2::RobotStaticStatus& message)
{
    domain::SelfConfig value;
    if (message.has_robot_id()) {
        value.robotId = message.robot_id();
        if (message.robot_id() >= 1 && message.robot_id() <= 7) value.team = domain::Team::Red;
        if (message.robot_id() >= 101 && message.robot_id() <= 107) value.team = domain::Team::Blue;
    }
    if (message.has_max_health()) value.maxHealth = message.max_health();
    if (message.has_level()) value.level = message.level();
    return value;
}

domain::SelfDynamic mapDynamic(const ::rm2026::v2::RobotDynamicStatus& message)
{
    domain::SelfDynamic value;
    if (message.has_current_health()) value.health = message.current_health();
    if (message.has_remaining_ammo()) value.permittedShots = message.remaining_ammo();
    if (message.has_current_heat()) value.heat = message.current_heat();
    if (message.has_last_projectile_fire_rate()) value.lastProjectileFireRate = message.last_projectile_fire_rate();
    return value;
}

domain::Units mapUnits(const ::rm2026::v2::GlobalUnitStatus& message)
{
    domain::Units value;
    if (message.has_base_health()) value.ally.base.health = message.base_health();
    if (message.has_base_status()) value.ally.base.status = message.base_status();
    if (message.has_base_shield()) value.ally.base.shield = message.base_shield();
    if (message.has_outpost_health()) value.ally.outpost.health = message.outpost_health();
    if (message.has_outpost_status()) value.ally.outpost.status = message.outpost_status();
    if (message.has_enemy_base_health()) value.enemy.base.health = message.enemy_base_health();
    if (message.has_enemy_base_status()) value.enemy.base.status = message.enemy_base_status();
    if (message.has_enemy_base_shield()) value.enemy.base.shield = message.enemy_base_shield();
    if (message.has_enemy_outpost_health()) value.enemy.outpost.health = message.enemy_outpost_health();
    if (message.has_enemy_outpost_status()) value.enemy.outpost.status = message.enemy_outpost_status();
    value.ally.outpost.life = outpostLife(value.ally.outpost);
    value.enemy.outpost.life = outpostLife(value.enemy.outpost);
    if (message.robot_health_size() == 10) {
        for (int index = 0; index < 5; ++index) {
            value.ally.robots[index].health = message.robot_health(index);
            value.enemy.robots[index].health = message.robot_health(index + 5);
        }
    }
    return value;
}

domain::Economy mapEconomy(const ::rm2026::v2::GlobalLogisticsStatus& message)
{
    domain::Economy value;
    if (message.has_remaining_economy()) value.remaining = message.remaining_economy();
    return value;
}

domain::Game mapGame(const ::rm2026::v2::GameStatus& message)
{
    domain::Game value;
    if (message.has_current_round()) value.currentRound = message.current_round();
    if (message.has_total_rounds()) value.totalRounds = message.total_rounds();
    if (message.has_red_score()) value.redScore = message.red_score();
    if (message.has_blue_score()) value.blueScore = message.blue_score();
    if (message.has_current_stage()) {
        value.stageCode = message.current_stage();
        if (message.current_stage() <= 5) value.phase = static_cast<domain::Phase>(message.current_stage());
    }
    if (message.has_stage_countdown_sec()) value.countdownSec = message.stage_countdown_sec();
    if (message.has_stage_elapsed_sec()) value.elapsedSec = message.stage_elapsed_sec();
    if (message.has_is_paused()) value.paused = message.is_paused();
    if (message.has_game_result()) value.reportedResult = message.game_result();
    if (message.has_end_reason()) value.reportedEndReason = message.end_reason();
    if (value.phase == domain::Phase::Settlement) {
        if (value.reportedResult && *value.reportedResult <= 2) value.result = value.reportedResult;
        if (value.reportedEndReason && *value.reportedEndReason >= 1 && *value.reportedEndReason <= 9)
            value.endReason = value.reportedEndReason;
    }
    return value;
}

domain::Penalty mapPenalty(const ::rm2026::v2::PenaltyInfo& message)
{
    domain::Penalty value;
    if (message.has_penalty_type()) {
        value.typeCode = message.penalty_type();
        if (message.penalty_type() >= 1 && message.penalty_type() <= 6)
            value.kind = static_cast<domain::PenaltyKind>(message.penalty_type());
    }
    if (message.has_penalty_effect_sec()) value.effectSec = message.penalty_effect_sec();
    if (message.has_total_penalty_num()) value.total = message.total_penalty_num();
    return value;
}

domain::Modules mapModules(const ::rm2026::v2::RobotModuleStatus& message)
{
    domain::Modules value;
    // PDF 60-61: all eleven modules share codes 0/1/2, not bool/nonzero.
    if (message.has_power_manager()) value.powerManager = module(message.power_manager());
    if (message.has_rfid()) value.rfid = module(message.rfid());
    if (message.has_light_strip()) value.lightStrip = module(message.light_strip());
    if (message.has_small_shooter()) value.smallShooter = module(message.small_shooter());
    if (message.has_big_shooter()) value.bigShooter = module(message.big_shooter());
    if (message.has_uwb()) value.uwb = module(message.uwb());
    if (message.has_armor()) value.armor = module(message.armor());
    if (message.has_video_transmission()) value.videoTransmission = module(message.video_transmission());
    if (message.has_capacitor()) value.capacitor = module(message.capacitor());
    if (message.has_main_controller()) value.mainController = module(message.main_controller());
    if (message.has_laser_detection_module()) value.laserDetectionModule = module(message.laser_detection_module());
    return value;
}

domain::Respawn mapRespawn(const ::rm2026::v2::RobotRespawnStatus& message)
{
    domain::Respawn value;
    if (message.has_is_pending_respawn()) value.pending = message.is_pending_respawn();
    if (message.has_total_respawn_progress()) value.requiredProgress = message.total_respawn_progress();
    if (message.has_current_respawn_progress()) value.currentProgress = message.current_respawn_progress();
    if (message.has_can_free_respawn()) value.reportedCanFree = message.can_free_respawn();
    if (message.has_gold_cost_for_respawn()) value.goldCost = message.gold_cost_for_respawn();
    if (message.has_can_pay_for_respawn()) value.reportedCanPay = message.can_pay_for_respawn();
    return value;
}

domain::EventReport mapEvent(const ::rm2026::v2::Event& message)
{
    domain::EventReport value;
    if (message.has_event_id()) {
        value.id = message.event_id();
        if (message.event_id() >= 1 && message.event_id() <= 15)
            value.kind = static_cast<domain::EventKind>(message.event_id());
    }
    if (message.has_param()) value.param = message.param();
    return value;
}

domain::Position mapPosition(const ::rm2026::v2::RobotPosition& message)
{
    domain::Position value;
    if (message.has_x()) value.x = message.x();
    if (message.has_y()) value.y = message.y();
    if (message.has_z()) value.z = message.z();
    if (message.has_yaw()) value.yawDeg = message.yaw();
    if (message.has_robot_id()) value.reportedRobotId = message.robot_id();
    return value;
}

domain::BuffReport mapBuff(const ::rm2026::v2::Buff& message)
{
    domain::BuffReport value;
    if (message.has_robot_id()) value.reportedRobotId = message.robot_id();
    if (message.has_buff_type()) {
        value.typeCode = message.buff_type();
        if (message.buff_type() >= 1 && message.buff_type() <= 7)
            value.kind = static_cast<domain::BuffKind>(message.buff_type());
    }
    if (message.has_buff_level()) value.level = message.buff_level();
    if (message.has_buff_max_time()) value.maxTime = message.buff_max_time();
    if (message.has_buff_left_time()) value.leftTime = message.buff_left_time();
    return value;
}

domain::Performance mapPerformance(const ::rm2026::v2::RobotPerformanceSelectionSync& message)
{
    domain::Performance value;
    if (message.has_shooter()) value.shooter = message.shooter();
    if (message.has_chassis()) value.chassis = message.chassis();
    if (message.has_sentry_control()) value.sentryControl = message.sentry_control();
    return value;
}

domain::Deployment mapDeployment(const ::rm2026::v2::DeployModeStatusSync& message)
{
    domain::Deployment value;
    if (message.has_status()) {
        value.code = message.status();
        if (message.status() <= 1) value.state = static_cast<domain::DeployState>(message.status());
    }
    return value;
}

QJsonObject snapshotJson(const domain::Snapshot& snapshot)
{
    QJsonObject json;
    if (snapshot.selfConfig) {
        const auto& value = snapshot.selfConfig->value;
        QJsonObject config{{"team", value.team == domain::Team::Red ? "red" :
            value.team == domain::Team::Blue ? "blue" : "unknown"}};
        put(config, "robot_id", value.robotId);
        put(config, "max_health", value.maxHealth);
        put(config, "level", value.level);
        json.insert("self_config", config);
    }
    if (snapshot.selfDynamic) {
        const auto& value = snapshot.selfDynamic->value;
        QJsonObject dynamic;
        put(dynamic, "health", value.health);
        put(dynamic, "permitted_shots", value.permittedShots);
        put(dynamic, "heat", value.heat);
        put(dynamic, "last_projectile_fire_rate", value.lastProjectileFireRate);
        json.insert("self_dynamic", dynamic);
    }
    if (snapshot.units) json.insert("units", QJsonObject{
        {"ally", sideJson(snapshot.units->value.ally)}, {"enemy", sideJson(snapshot.units->value.enemy)}});
    if (snapshot.economy) {
        QJsonObject economy;
        put(economy, "remaining", snapshot.economy->value.remaining);
        json.insert("economy", economy);
    }
    if (snapshot.game) {
        const auto& value = snapshot.game->value;
        constexpr std::array phases{"not_started", "preparation", "self_check", "countdown", "playing", "settlement"};
        const int phase = static_cast<int>(value.phase);
        QJsonObject game{{"phase", phase >= 0 && phase < int(phases.size()) ? phases[phase] : "unknown"}};
        put(game, "current_round", value.currentRound);
        put(game, "total_rounds", value.totalRounds);
        put(game, "red_score", value.redScore);
        put(game, "blue_score", value.blueScore);
        put(game, "stage_code", value.stageCode);
        put(game, "countdown_sec", value.countdownSec);
        put(game, "elapsed_sec", value.elapsedSec);
        if (value.paused) game.insert("paused", *value.paused);
        put(game, "reported_result", value.reportedResult);
        put(game, "reported_end_reason", value.reportedEndReason);
        put(game, "result", value.result);
        put(game, "end_reason", value.endReason);
        json.insert("game", game);
    }
    if (snapshot.penalty) {
        const auto& value = snapshot.penalty->value;
        constexpr std::array kinds{"unknown", "yellow_card", "both_yellow_cards", "red_card", "power", "heat", "fire_rate"};
        const int kind = static_cast<int>(value.kind);
        QJsonObject penalty{{"kind", kind >= 0 && kind < int(kinds.size()) ? kinds[kind] : "unknown"}};
        put(penalty, "type_code", value.typeCode);
        put(penalty, "effect_sec", value.effectSec);
        put(penalty, "total", value.total);
        json.insert("penalty", penalty);
    }
    if (snapshot.modules) {
        const auto& value = snapshot.modules->value;
        QJsonObject modules;
        putModule(modules, "power_manager", value.powerManager);
        putModule(modules, "rfid", value.rfid);
        putModule(modules, "light_strip", value.lightStrip);
        putModule(modules, "small_shooter", value.smallShooter);
        putModule(modules, "big_shooter", value.bigShooter);
        putModule(modules, "uwb", value.uwb);
        putModule(modules, "armor", value.armor);
        putModule(modules, "video_transmission", value.videoTransmission);
        putModule(modules, "capacitor", value.capacitor);
        putModule(modules, "main_controller", value.mainController);
        putModule(modules, "laser_detection_module", value.laserDetectionModule);
        json.insert("modules", modules);
    }
    if (snapshot.respawn) {
        const auto& value = snapshot.respawn->value;
        QJsonObject respawn;
        if (value.pending) respawn.insert("pending", *value.pending);
        if (value.reportedCanFree) respawn.insert("reported_can_free", *value.reportedCanFree);
        if (value.reportedCanPay) respawn.insert("reported_can_pay", *value.reportedCanPay);
        put(respawn, "required_progress", value.requiredProgress);
        put(respawn, "current_progress", value.currentProgress);
        put(respawn, "gold_cost", value.goldCost);
        json.insert("respawn", respawn);
    }
    if (snapshot.latestEvent) {
        const auto& value = snapshot.latestEvent->value;
        constexpr std::array kinds{"unknown", "kill", "outpost_destroyed", "large_energy_result", "energy_activated",
            "ally_sniper_damage", "enemy_sniper_damage", "enemy_air_support", "enemy_air_support_countered",
            "dart_hit", "enemy_dart_gate_opened", "base_attacked", "enemy_outpost_stopped",
            "enemy_base_armor_opened", "enemy_assembly_forced_exit", "assembly_result"};
        const int kind = static_cast<int>(value.kind);
        QJsonObject event{{"kind", kind >= 0 && kind < int(kinds.size()) ? kinds[kind] : "unknown"}};
        put(event, "id", value.id);
        if (value.param) event.insert("param", QString::fromUtf8(value.param->data(), qsizetype(value.param->size())));
        json.insert("latest_event", event);
    }
    if (snapshot.position) {
        const auto& value = snapshot.position->value;
        QJsonObject position;
        put(position, "x", value.x);
        put(position, "y", value.y);
        put(position, "z", value.z);
        put(position, "yaw_deg", value.yawDeg);
        put(position, "reported_robot_id", value.reportedRobotId);
        json.insert("position", position);
    }
    if (snapshot.latestBuff) {
        const auto& value = snapshot.latestBuff->value;
        constexpr std::array kinds{"unknown", "attack", "defense_or_vulnerability", "cooling",
            "chassis_power", "recovery", "exchangeable_shots", "terrain_prewarning"};
        const int kind = static_cast<int>(value.kind);
        QJsonObject buff{{"kind", kind >= 0 && kind < int(kinds.size()) ? kinds[kind] : "unknown"}};
        put(buff, "reported_robot_id", value.reportedRobotId);
        put(buff, "type_code", value.typeCode);
        put(buff, "level", value.level);
        put(buff, "max_time", value.maxTime);
        put(buff, "left_time", value.leftTime);
        json.insert("latest_buff", buff);
    }
    if (snapshot.performance) {
        const auto& value = snapshot.performance->value;
        QJsonObject performance;
        put(performance, "shooter", value.shooter);
        put(performance, "chassis", value.chassis);
        put(performance, "sentry_control", value.sentryControl);
        json.insert("performance", performance);
    }
    if (snapshot.deployment) {
        const auto& value = snapshot.deployment->value;
        QJsonObject deployment{{"state", value.state == domain::DeployState::NotDeployed ? "not_deployed" :
            value.state == domain::DeployState::Deployed ? "deployed" : "unknown"}};
        put(deployment, "code", value.code);
        json.insert("deployment", deployment);
    }
    return json;
}
} // namespace rm27::protocol::rm2026
